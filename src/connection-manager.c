/*
 * connection-manager.c - HazeConnectionManager source
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <string.h>

#include <glib.h>
#include <dbus/dbus-protocol.h>

#include <libpurple/prpl.h>
#include <libpurple/accountopt.h>

#include "connection-manager.h"
#include "debug.h"

G_DEFINE_TYPE(HazeConnectionManager,
    haze_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/** These are protocols for which stripping off the "prpl-" prefix is not
 *  sufficient, or for which special munging has to be done.
 */
static HazeProtocolInfo known_protocol_info[] = {
    { "gadugadu",   "prpl-gg",          NULL, "" },
    { "groupwise",  "prpl-novell",      NULL, "" },
    { "irc",        "prpl-irc",         NULL, "" },
    { "sametime",   "prpl-meanwhile",   NULL, "" },
    { "local-xmpp", "prpl-bonjour",     NULL, "" },
    { "jabber",     "prpl-jabber",      NULL, "connect_server:server" },
    { NULL,         NULL,               NULL, "" }
};

static void *
_haze_cm_alloc_params (void)
{
    /* (gchar *) => (GValue *) */
    return g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
        (GDestroyNotify) tp_g_value_slice_free);
}

static void
_haze_cm_free_params (void *p)
{
    GHashTable *params = (GHashTable *)p;
    g_hash_table_unref (params);
}

static void
_haze_cm_set_param (const TpCMParamSpec *paramspec,
                    const GValue *value,
                    gpointer params_)
{
    GHashTable *params = (GHashTable *) params_;
    GValue *value_copy = tp_g_value_slice_new (paramspec->gtype);
    gchar *prpl_param_name = (gchar *) paramspec->setter_data;

    g_assert (G_VALUE_TYPE (value) == G_VALUE_TYPE (value_copy));

    g_value_copy (value, value_copy);

    g_debug ("setting parameter %s (telepathy name %s)",
        prpl_param_name, paramspec->name);

    g_hash_table_insert (params, prpl_param_name, value_copy);
}

struct _protocol_info_foreach_data
{
    TpCMProtocolSpec *protocols;
    guint index;
};

static TpCMParamSpec *
_build_paramspecs (HazeProtocolInfo *hpi)
{
    const TpCMParamSpec account_spec =
        { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
        TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0, NULL, NULL };
    TpCMParamSpec password_spec =
        { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
        TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0, NULL, NULL };
    TpCMParamSpec paramspec = { NULL, NULL, 0, 0, NULL, 0, NULL, NULL };

    GArray *paramspecs = g_array_new (TRUE, TRUE, sizeof (TpCMParamSpec));
    GList *opts;

    /* Mad "deserializing" of "foo:bar,baz:badger" to a hash table */
    GHashTable *parameter_map = g_hash_table_new (g_str_hash, g_str_equal);
    gchar **map_chunks = g_strsplit_set (hpi->parameter_map, ",:", 0);
    int i;
    for (i = 0; map_chunks[i] != NULL; i = i + 2)
    {
        g_assert (map_chunks[i+1] != NULL);
        g_hash_table_insert (parameter_map, map_chunks[i], map_chunks[i+1]);
    }

    /* TODO: Some protocols shouldn't actually have account parameters (I think
     *       local-xmpp is one example)
     */
    g_array_append_val (paramspecs, account_spec);

    /* Password parameter: */
    if (!(hpi->prpl_info->options & OPT_PROTO_NO_PASSWORD))
    {
        if (hpi->prpl_info->options & OPT_PROTO_PASSWORD_OPTIONAL)
            password_spec.flags = 0;
        g_array_append_val (paramspecs, password_spec);
    }

    for (opts = hpi->prpl_info->protocol_options; opts; opts = opts->next)
    {
        PurpleAccountOption *option = (PurpleAccountOption *)opts->data;

        gchar *name = g_hash_table_lookup (parameter_map, option->pref_name);
        /* TODO: These are never free'd.  They'd only be free'd when the
         *       class is unloaded.  Is there any point in freeing them?
         */
        paramspec.name = g_strdup (name ? name : option->pref_name);
        paramspec.setter_data = option->pref_name;

        switch (purple_account_option_get_type (option))
        {
            case PURPLE_PREF_BOOLEAN:
                paramspec.dtype = DBUS_TYPE_BOOLEAN_AS_STRING;
                paramspec.gtype = G_TYPE_BOOLEAN;
                paramspec.def = GINT_TO_POINTER (
                    purple_account_option_get_default_bool (option));
                break;
            case PURPLE_PREF_INT:
                paramspec.dtype = DBUS_TYPE_INT32_AS_STRING;
                paramspec.gtype = G_TYPE_INT;
                paramspec.def = GINT_TO_POINTER (
                    purple_account_option_get_default_int (option));
                break;
            case PURPLE_PREF_STRING:
                paramspec.dtype = DBUS_TYPE_STRING_AS_STRING;
                paramspec.gtype = G_TYPE_STRING;
                paramspec.def =
                    purple_account_option_get_default_string (option);
                break;
            default:
                g_warning ("account option %s has unknown type %u; ignoring",
                    option->pref_name, purple_account_option_get_type (option));
                continue;
        }
        /* TODO: does libpurple ever require a parameter besides the username
         *       and possibly password?
         */
        paramspec.flags = 0;
        g_array_append_val (paramspecs, paramspec);
    }

    g_hash_table_destroy (parameter_map);
    g_strfreev (map_chunks);

    return (TpCMParamSpec *) g_array_free (paramspecs, FALSE);
}

static void
_protocol_info_foreach (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
    HazeProtocolInfo *info = (HazeProtocolInfo *)value;
    struct _protocol_info_foreach_data *data =
        (struct _protocol_info_foreach_data *)user_data;
    TpCMProtocolSpec *protocol = &(data->protocols[data->index]);

    protocol->name = info->tp_protocol_name;
    protocol->parameters = _build_paramspecs (info);
    protocol->params_new = _haze_cm_alloc_params;
    protocol->params_free = _haze_cm_free_params;
    protocol->set_param = _haze_cm_set_param;

    (data->index)++;
}

static TpCMProtocolSpec *
get_protocols (HazeConnectionManagerClass *klass)
{
    struct _protocol_info_foreach_data foreach_data;
    TpCMProtocolSpec *protocols;
    guint n_protocols;

    n_protocols = g_hash_table_size (klass->protocol_info_table);
    foreach_data.protocols = protocols = (TpCMProtocolSpec *)
        g_slice_alloc0 (sizeof (TpCMProtocolSpec) * (n_protocols + 1));
    foreach_data.index = 0;

    g_hash_table_foreach (klass->protocol_info_table, _protocol_info_foreach,
        &foreach_data);

    return protocols;
}

HazeConnection *
haze_connection_manager_get_haze_connection (HazeConnectionManager *self,
                                             PurpleAccount *account)
{
    HazeConnection *hc;
    GList *l = self->connections;

    while (l != NULL) {
        hc = l->data;
        if(hc->account == account) {
            return hc;
        }
    }

    return NULL;
}

static void
connection_shutdown_finished_cb (TpBaseConnection *conn,
                                 gpointer data)
{
    HazeConnectionManager *self = HAZE_CONNECTION_MANAGER (data);

    self->connections = g_list_remove(self->connections, conn);
}

static TpBaseConnection *
_haze_connection_manager_new_connection (TpBaseConnectionManager *base,
                                         const gchar *proto,
                                         TpIntSet *params_present,
                                         void *parsed_params,
                                         GError **error)
{
    HazeConnectionManager *cm = HAZE_CONNECTION_MANAGER(base);
    HazeConnectionManagerClass *klass = HAZE_CONNECTION_MANAGER_GET_CLASS (cm);
    GHashTable *params = (GHashTable *)parsed_params;
    HazeProtocolInfo *info =
        g_hash_table_lookup (klass->protocol_info_table, proto);
    HazeConnection *conn = g_object_new (HAZE_TYPE_CONNECTION,
                                         "protocol",        proto,
                                         "protocol-info",   info,
                                         "parameters",      params,
                                         NULL);

    cm->connections = g_list_prepend(cm->connections, conn);
    g_signal_connect (conn, "shutdown-finished",
                      G_CALLBACK (connection_shutdown_finished_cb),
                      cm);

    return (TpBaseConnection *) conn;
}

/** Frees the slice-allocated HazeProtocolInfo pointed to by @a data.  Useful
 *  as the value-destroying callback in a hash table.
 */
static void
_protocol_info_slice_free (gpointer data)
{
    g_slice_free (HazeProtocolInfo, data);
}

/** Predicate for g_hash_table_find to search on prpl_id.
 *  @param key      (const gchar *)tp_protocol_name
 *  @param value    (HazeProtocolInfo *)info
 *  @param data     (const gchar *)prpl_id
 *  @return @c TRUE iff info->prpl_id eq prpl_id
 */
static gboolean
_compare_protocol_id (gpointer key,
                      gpointer value,
                      gpointer data)
{
    HazeProtocolInfo *info = (HazeProtocolInfo *)value;
    const gchar *prpl_id = (const gchar *)data;
    return (!strcmp (info->prpl_id, prpl_id));
}

static void
get_values_foreach (gpointer key,
                    gpointer value,
                    gpointer data)
{
    GList **values = data;

    *values = g_list_prepend (*values, value);
}

/* Equivalent to g_hash_table_get_values, which only exists in GLib >=2.14. */
static GList *
haze_g_hash_table_get_values (GHashTable *table)
{
    GList *values = NULL;

    g_hash_table_foreach (table, get_values_foreach, &values);

    return values;
}

static void _init_protocol_table (HazeConnectionManagerClass *klass)
{
    GHashTable *table;
    HazeProtocolInfo *i, *info;
    PurplePlugin *plugin;
    PurplePluginInfo *p_info;
    PurplePluginProtocolInfo *prpl_info;
    GList *iter;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                   _protocol_info_slice_free);

    for (i = known_protocol_info; i->prpl_id != NULL; i++)
    {
        plugin = purple_find_prpl (i->prpl_id);
        if (!plugin)
            continue;

        info = g_slice_new (HazeProtocolInfo);

        info->prpl_id = i->prpl_id;
        info->tp_protocol_name = i->tp_protocol_name;
        info->prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (plugin);
        info->parameter_map = i->parameter_map;

        g_hash_table_insert (table, info->tp_protocol_name, info);
    }

    for (iter = purple_plugins_get_protocols (); iter; iter = iter->next)
    {
        plugin = (PurplePlugin *)iter->data;
        p_info = plugin->info;
        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (plugin);

        if (g_hash_table_find (table, _compare_protocol_id, p_info->id))
            continue; /* already in the table from the previous loop */

        info = g_slice_new (HazeProtocolInfo);
        info->prpl_id = p_info->id;
        if (g_str_has_prefix (p_info->id, "prpl-"))
            info->tp_protocol_name = (p_info->id + 5);
        else
        {
            g_warning ("prpl '%s' has a dumb id; spank its author", p_info->id);
            info->tp_protocol_name = p_info->id;
        }
        info->prpl_info = prpl_info;
        info->parameter_map = "";

        g_hash_table_insert (table, info->tp_protocol_name, info);
    }

    {
        GList *protocols = haze_g_hash_table_get_values (table);
        GList *l;
        GString *debug_string = g_string_new ("");

        for (l = protocols; l; l = l->next)
        {
            info = l->data;
            g_string_append (debug_string, info->tp_protocol_name);
            if (l->next)
                g_string_append (debug_string, ", ");
        }

        DEBUG ("Found protocols %s", debug_string->str);

        g_list_free (protocols);
        g_string_free (debug_string, TRUE);
    }

    klass->protocol_info_table = table;
}

static void
haze_connection_manager_class_init (HazeConnectionManagerClass *klass)
{
    TpBaseConnectionManagerClass *base_class =
        (TpBaseConnectionManagerClass *)klass;

    _init_protocol_table (klass);

    base_class->new_connection = _haze_connection_manager_new_connection;
    base_class->cm_dbus_name = "haze";
    base_class->protocol_params = get_protocols (klass);
}

static void
haze_connection_manager_init (HazeConnectionManager *self)
{
    DEBUG ("Initializing (HazeConnectionManager *)%p", self);
}

HazeConnectionManager *
haze_connection_manager_get (void) {
    static HazeConnectionManager *manager = NULL;
    if (G_UNLIKELY(manager == NULL)) {
        manager = g_object_new (HAZE_TYPE_CONNECTION_MANAGER, NULL);
    }
    g_assert (manager != NULL);
    return manager;
}
