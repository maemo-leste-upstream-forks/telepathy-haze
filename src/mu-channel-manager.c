/*
 * mu-channel-manager.c - HazeMuChannelManager source
 *
 * Copyright (C) 2023 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
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

#include "config.h"
#include "mu-channel-manager.h"

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/interfaces.h>

#include "debug.h"
#include "connection.h"
#include "conversation-ui.h"
#include "mu-channel.h"

struct _HazeMuChannelManagerPrivate {
    HazeConnection *conn;
    GHashTable *channels;
    gulong status_changed_id;
};

static void haze_mu_channel_manager_iface_init (gpointer, gpointer);
static void close_all (HazeMuChannelManager *self);
static void haze_mu_create_conversation (PurpleConversation *conv);
static void haze_mu_destroy_conversation (PurpleConversation *conv);
static void haze_mu_write_chat (PurpleConversation *conv, const char *name,
    const char *xhtml_message, PurpleMessageFlags flags, time_t mtime);
static void haze_mu_write_conv (PurpleConversation *conv, const char *name,
    const char *alias, const char *message, PurpleMessageFlags flags,
    time_t mtime);
static void haze_mu_chat_add_users(PurpleConversation *conv, GList *cbuddies,
    gboolean new_arrivals);
static void haze_mu_chat_remove_users(PurpleConversation *conv, GList *users);

G_DEFINE_TYPE_WITH_CODE(HazeMuChannelManager,
    haze_mu_channel_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      haze_mu_channel_manager_iface_init))

/* properties: */
enum {
    PROP_CONNECTION = 1,

    LAST_PROPERTY
};

static void
haze_mu_channel_manager_init (HazeMuChannelManager *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        HAZE_TYPE_MU_CHANNEL_MANAGER, HazeMuChannelManagerPrivate);

    self->priv->channels = g_hash_table_new_full (NULL, NULL,
        NULL, g_object_unref);
    self->priv->conn = NULL;
}

static void
haze_mu_channel_manager_dispose (GObject *object)
{
    HazeMuChannelManager *self = HAZE_MU_CHANNEL_MANAGER (object);

    close_all (self);
    g_assert (self->priv->channels == NULL);

    G_OBJECT_CLASS (haze_mu_channel_manager_parent_class)->dispose (object);
}

static void
haze_mu_channel_manager_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    HazeMuChannelManager *self = HAZE_MU_CHANNEL_MANAGER (object);

    switch (property_id) {
        case PROP_CONNECTION:
            g_value_set_object (value, self->priv->conn);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_mu_channel_manager_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    HazeMuChannelManager *self = HAZE_MU_CHANNEL_MANAGER (object);

    switch (property_id) {
        case PROP_CONNECTION:
            self->priv->conn = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
status_changed_cb (HazeConnection *conn, guint status, guint reason,
                   HazeMuChannelManager *self)
{
    if (status == TP_CONNECTION_STATUS_DISCONNECTED)
        close_all (self);
}

static void
haze_mu_channel_manager_constructed (GObject *object)
{
    HazeMuChannelManager *self = HAZE_MU_CHANNEL_MANAGER (object);

    G_OBJECT_CLASS (haze_mu_channel_manager_parent_class)->constructed (object);

    self->priv->status_changed_id = g_signal_connect (self->priv->conn,
        "status-changed", (GCallback) status_changed_cb, self);
}

static void
mu_channel_closed_cb (HazeMUChannel *chan, gpointer user_data)
{
    HazeMuChannelManager *self = HAZE_MU_CHANNEL_MANAGER (user_data);
    TpHandle contact_handle;
    guint really_destroyed;

    tp_channel_manager_emit_channel_closed_for_object (self,
        TP_EXPORTABLE_CHANNEL (chan));

    if (self->priv->channels)
    {
        g_object_get (chan,
            "handle", &contact_handle,
            "channel-destroyed", &really_destroyed,
            NULL);

        if (really_destroyed)
        {
            DEBUG ("removing channel with handle %u", contact_handle);
            g_hash_table_remove (self->priv->channels,
                GUINT_TO_POINTER (contact_handle));
        }
        else
        {
            DEBUG ("reopening channel with handle %u due to pending messages",
                contact_handle);
            tp_channel_manager_emit_new_channel (self,
                (TpExportableChannel *) chan, NULL);
        }
    }
}

static HazeMUChannel *
new_mu_channel (HazeMuChannelManager *self, TpHandle handle, TpHandle initiator,
    gpointer request_token)
{
    TpBaseConnection *conn;
    HazeMUChannel *chan;
    char *object_path;
    GSList *requests = NULL;

    g_assert (HAZE_IS_MU_CHANNEL_MANAGER (self));

    conn = (TpBaseConnection *) self->priv->conn;

    g_assert (!g_hash_table_lookup (self->priv->channels,
          GINT_TO_POINTER (handle)));

    object_path = g_strdup_printf ("%s/MuChannel%u", conn->object_path, handle);

    chan = g_object_new (HAZE_TYPE_MU_CHANNEL,
                         "connection", self->priv->conn,
                         "object-path", object_path,
                         "handle", handle,
                         "initiator-handle", initiator,
                         NULL);

    DEBUG ("Created MU channel with object path %s", object_path);

    g_signal_connect (chan, "closed", G_CALLBACK (mu_channel_closed_cb), self);

    g_hash_table_insert (self->priv->channels, GINT_TO_POINTER (handle), chan);

    haze_mu_channel_start (chan);

    if (request_token != NULL)
        requests = g_slist_prepend (requests, request_token);

    tp_channel_manager_emit_new_channel (self,
        TP_EXPORTABLE_CHANNEL (chan), requests);
    g_slist_free (requests);

    g_free (object_path);

    return chan;
}

static HazeMUChannel *
get_mu_channel (HazeMuChannelManager *self, TpHandle handle, TpHandle initiator,
    gpointer request_token, gboolean *created)
{
    HazeMUChannel *chan =
        g_hash_table_lookup (self->priv->channels, GINT_TO_POINTER (handle));

    if (chan)
    {
        if (created)
            *created = FALSE;
    }
    else
    {
        chan = new_mu_channel (self, handle, initiator, request_token);

        if (created)
            *created = TRUE;
    }

    g_assert (chan);

    return chan;
}

static void
conversation_updated_cb (PurpleConversation *conv, PurpleConvUpdateType type,
    gpointer unused)
{
    PurpleAccount *account = purple_conversation_get_account (conv);
    HazeMuChannelManager *muc_manager =
        ACCOUNT_GET_HAZE_CONNECTION (account)->muc_manager;
    HazeConversationUiData *ui_data;
    HazeMUChannel *chan;

    PurpleTypingState typing;
    TpChannelChatState state;

    if (type != PURPLE_CONV_UPDATE_TYPING)
        return;

    if (conv->type != PURPLE_CONV_TYPE_CHAT)
        return;

    ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    typing = purple_conv_im_get_typing_state (PURPLE_CONV_IM (conv));

    switch (typing)
    {
        case PURPLE_TYPING:
            state = TP_CHANNEL_CHAT_STATE_COMPOSING;
            break;
        case PURPLE_TYPED:
            state = TP_CHANNEL_CHAT_STATE_PAUSED;
            break;
        case PURPLE_NOT_TYPING:
            state = TP_CHANNEL_CHAT_STATE_ACTIVE;
            break;
        default:
            g_assert_not_reached ();
    }

    chan = get_mu_channel (muc_manager, ui_data->contact_handle,
        ui_data->contact_handle, NULL, NULL);

    tp_svc_channel_interface_chat_state_emit_chat_state_changed (
        (TpSvcChannelInterfaceChatState *)chan, ui_data->contact_handle, state);
}

static void
haze_mu_channel_manager_class_init (HazeMuChannelManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    void *conv_handle = purple_conversations_get_handle();
    GParamSpec *param_spec;
    PurpleConversationUiOps *ui_ops = haze_get_conversation_ui_ops();

    object_class->constructed = haze_mu_channel_manager_constructed;
    object_class->dispose = haze_mu_channel_manager_dispose;
    object_class->get_property = haze_mu_channel_manager_get_property;
    object_class->set_property = haze_mu_channel_manager_set_property;

    param_spec = g_param_spec_object ("connection", "HazeConnection object",
                                      "Haze connection object that owns this "
                                      "MU channel manager object.",
                                      HAZE_TYPE_CONNECTION,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NICK |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

    g_type_class_add_private (object_class,
        sizeof(HazeMuChannelManagerPrivate));

    purple_signal_connect (conv_handle, "conversation-updated", klass,
        (PurpleCallback) conversation_updated_cb, NULL);
    haze_set_conversation_ui_chat_create_conversation(
        haze_mu_create_conversation);
    haze_set_conversation_ui_chat_destroy_conversation(
        haze_mu_destroy_conversation);
    haze_set_conversation_ui_chat_write_conv(haze_mu_write_conv);
    ui_ops->write_chat = haze_mu_write_chat;
    ui_ops->chat_add_users = haze_mu_chat_add_users;
    ui_ops->chat_remove_users = haze_mu_chat_remove_users;
}

static void
close_all (HazeMuChannelManager *self)
{
    GHashTable *tmp;

    DEBUG ("closing im channels");

    if (self->priv->channels)
    {
        tmp = self->priv->channels;
        self->priv->channels = NULL;
        g_hash_table_destroy (tmp);
    }

    if (self->priv->status_changed_id != 0)
    {
        g_signal_handler_disconnect (self->priv->conn,
            self->priv->status_changed_id);

        self->priv->status_changed_id = 0;
    }
}

struct _ForeachData
{
    TpExportableChannelFunc foreach;
    gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
    struct _ForeachData *data = (struct _ForeachData *) user_data;
    TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

    data->foreach (chan, data->user_data);
}

static void
haze_mu_channel_manager_foreach (TpChannelManager *iface,
                                 TpExportableChannelFunc foreach,
                                 gpointer user_data)
{
    HazeMuChannelManager *self = HAZE_MU_CHANNEL_MANAGER (iface);
    struct _ForeachData data;

    data.user_data = user_data;
    data.foreach = foreach;

    g_hash_table_foreach (self->priv->channels, _foreach_slave, &data);
}


static void
haze_mu_write_chat (PurpleConversation *conv, const char *who,
    const char *xhtml_message, PurpleMessageFlags flags, time_t mtime)
{
    PurpleAccount *account = purple_conversation_get_account (conv);
    HazeMuChannelManager *muc_manager =
        ACCOUNT_GET_HAZE_CONNECTION (account)->muc_manager;
    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    HazeMUChannel *chan = get_mu_channel (muc_manager, ui_data->contact_handle,
        ui_data->contact_handle, NULL, NULL);

    haze_mu_channel_receive (chan, who, xhtml_message, flags, mtime);
}

static void
haze_mu_write_conv (PurpleConversation *conv, const char *name,
    const char *alias, const char *message, PurpleMessageFlags flags,
    time_t mtime)
{
    haze_mu_write_chat (conv, name, message, flags, mtime);
}

static void
haze_mu_chat_add_users(PurpleConversation *conv, GList *cbuddies,
    gboolean new_arrivals)
{
    PurpleAccount *account = purple_conversation_get_account (conv);
    HazeMuChannelManager *muc_manager =
        ACCOUNT_GET_HAZE_CONNECTION (account)->muc_manager;
    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    HazeMUChannel *chan = get_mu_channel (muc_manager, ui_data->contact_handle,
        ui_data->contact_handle, NULL, NULL);

    haze_mu_channel_add_users(chan, cbuddies, new_arrivals);
}

static void
haze_mu_chat_remove_users(PurpleConversation *conv, GList *users)
{
    PurpleAccount *account = purple_conversation_get_account (conv);
    HazeMuChannelManager *muc_manager =
        ACCOUNT_GET_HAZE_CONNECTION (account)->muc_manager;
    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    HazeMUChannel *chan = get_mu_channel (muc_manager, ui_data->contact_handle,
        ui_data->contact_handle, NULL, NULL);

    haze_mu_channel_remove_users(chan, users);
}

static void
haze_mu_create_conversation (PurpleConversation *conv)
{
    PurpleAccount *account = purple_conversation_get_account (conv);

    HazeMuChannelManager *muc_manager =
        ACCOUNT_GET_HAZE_CONNECTION (account)->muc_manager;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (muc_manager->priv->conn);
    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    const gchar *who = purple_conversation_get_name (conv);

    HazeConversationUiData *ui_data;

    DEBUG ("(PurpleConversation *)%p created", conv);

    g_assert (who);

    conv->ui_data = ui_data = g_slice_new0 (HazeConversationUiData);

    ui_data->contact_handle = tp_handle_ensure (contact_repo, who, NULL, NULL);
    g_assert (ui_data->contact_handle);
}

static void
haze_mu_destroy_conversation (PurpleConversation *conv)
{
    HazeConversationUiData *ui_data;

    DEBUG ("(PurpleConversation *)%p destroyed", conv);

    ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);

    if (ui_data->resend_typing_timeout_id)
        g_source_remove (ui_data->resend_typing_timeout_id);

    g_slice_free (HazeConversationUiData, ui_data);
    conv->ui_data = NULL;
}

static const gchar * const allowed_properties[] =
{
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
    NULL
};

static const gchar * const allowed_room_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    TP_PROP_CHANNEL_INTERFACE_ROOM_ROOM_NAME,
    NULL
};

static void
haze_mu_channel_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func, gpointer user_data)
{
    static GHashTable *handle_fixed = NULL, *room_name_fixed = NULL;

    if (G_UNLIKELY (handle_fixed == NULL))
    {
      handle_fixed = tp_asv_new (
          TP_PROP_CHANNEL_CHANNEL_TYPE,
          G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
          G_TYPE_UINT, TP_HANDLE_TYPE_ROOM,
          NULL);
      room_name_fixed = tp_asv_new (
          TP_PROP_CHANNEL_CHANNEL_TYPE,
          G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
          NULL);
    }

    func (type, handle_fixed, allowed_properties, user_data);
    func (type, room_name_fixed, allowed_room_properties, user_data);
}

static gboolean
haze_mu_channel_manager_request (HazeMuChannelManager *self,
    gpointer request_token, GHashTable *request_properties,
    gboolean require_new)
{
    static const gchar * const fixed_properties[] = {
        TP_PROP_CHANNEL_CHANNEL_TYPE,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
        NULL
    };
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
    TpHandle handle;
    gboolean created;
    HazeMUChannel *chan;
    GError *error = NULL;

    if (tp_strdiff (tp_asv_get_string (request_properties,
            TP_PROP_CHANNEL_CHANNEL_TYPE), TP_IFACE_CHANNEL_TYPE_TEXT))
    {
        return FALSE;
    }

    if (tp_asv_get_uint32 (request_properties,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL) != TP_HANDLE_TYPE_ROOM)
    {
        return FALSE;
    }

    handle = tp_asv_get_uint32 (request_properties,
        TP_PROP_CHANNEL_TARGET_HANDLE, NULL);

    g_assert (handle != 0);

    if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          fixed_properties, allowed_properties, &error))
    {
        goto error;
    }

    chan = get_mu_channel (self, handle, base_conn->self_handle,
        request_token, &created);

    g_assert (chan != NULL);

    if (!created)
    {
        if (require_new)
        {
            tp_channel_manager_emit_request_failed (self, request_token,
                TP_ERROR, TP_ERROR_NOT_AVAILABLE, "Room already exists");
        }
        else
        {
            tp_channel_manager_emit_request_already_satisfied (self,
                request_token, TP_EXPORTABLE_CHANNEL (chan));
        }
    }

    return TRUE;

error:
    tp_channel_manager_emit_request_failed (self, request_token,
        error->domain, error->code, error->message);
    g_error_free (error);

    return TRUE;
}

static gboolean
haze_mu_channel_manager_create_channel (TpChannelManager *manager,
    gpointer request_token, GHashTable *request_properties)
{
    return haze_mu_channel_manager_request (HAZE_MU_CHANNEL_MANAGER (manager),
        request_token, request_properties, TRUE);
}

static gboolean
haze_mu_channel_manager_ensure_channel (TpChannelManager *manager,
    gpointer request_token, GHashTable *request_properties)
{
    return haze_mu_channel_manager_request (HAZE_MU_CHANNEL_MANAGER (manager),
        request_token, request_properties, FALSE);
}

static void
haze_mu_channel_manager_iface_init (gpointer g_iface,
                                    gpointer iface_data G_GNUC_UNUSED)
{
    TpChannelManagerIface *iface = g_iface;

    iface->foreach_channel = haze_mu_channel_manager_foreach;
    iface->type_foreach_channel_class =
        haze_mu_channel_manager_type_foreach_channel_class;
    iface->create_channel = haze_mu_channel_manager_create_channel;
    iface->ensure_channel = haze_mu_channel_manager_ensure_channel;
    iface->request_channel = haze_mu_channel_manager_ensure_channel;
}
