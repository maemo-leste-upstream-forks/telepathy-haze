/*
 * connection.c - HazeConnection source
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

#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>

#include <libpurple/accountopt.h>
#include <libpurple/version.h>

#include "debug.h"
#include "defines.h"
#include "connection-manager.h"
#include "connection.h"
#include "connection-presence.h"
#include "connection-aliasing.h"
#include "connection-avatars.h"

enum
{
    PROP_USERNAME = 1,
    PROP_PASSWORD,
    PROP_SERVER,
    PROP_PROTOCOL_INFO,

    LAST_PROPERTY
};

G_DEFINE_TYPE_WITH_CODE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
        tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        haze_connection_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
        haze_connection_avatars_iface_init);
    );

typedef struct _HazeConnectionPrivate
{
    char *username;
    char *password;
    char *server;

    HazeProtocolInfo *protocol_info;
} HazeConnectionPrivate;

#define HAZE_CONNECTION_GET_PRIVATE(o) \
  ((HazeConnectionPrivate *)o->priv)

#define PC_GET_BASE_CONN(pc) \
    (ACCOUNT_GET_TP_BASE_CONNECTION (purple_connection_get_account (pc)))

void
connected_cb (PurpleConnection *pc)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);
    HazeConnection *conn = HAZE_CONNECTION (base_conn);
    PurplePluginProtocolInfo *prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (conn);

    if (prpl_info->icon_spec.format != NULL)
    {
        static const gchar *avatar_ifaces[] = {
            TP_IFACE_CONNECTION_INTERFACE_AVATARS,
            NULL };
        tp_base_connection_add_interfaces (base_conn, avatar_ifaces);
    }

    tp_base_connection_change_status (base_conn,
        TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

#if PURPLE_VERSION_CHECK(2,3,0)
static void
haze_report_disconnect_reason (PurpleConnection *gc,
                               PurpleConnectionError reason,
                               const char *text)
{
    PurpleAccount *account = purple_connection_get_account (gc);
    TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    TpConnectionStatusReason tp_reason;

    switch (reason)
    {
        case PURPLE_CONNECTION_ERROR_NETWORK_ERROR:
        /* TODO: this isn't the right mapping.  should this map to
         *       NoneSpecified?
         */
        case PURPLE_CONNECTION_ERROR_OTHER_ERROR:
            tp_reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
            break;
        case PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED:
        case PURPLE_CONNECTION_ERROR_INVALID_USERNAME:
        /* TODO: the following don't really match the tp reason but it's
         *       the nearest match.  Invalid settings shouldn't exist in the
         *       first place.
         */
        case PURPLE_CONNECTION_ERROR_AUTHENTICATION_IMPOSSIBLE:
        case PURPLE_CONNECTION_ERROR_INVALID_SETTINGS:
            tp_reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
            break;
        case PURPLE_CONNECTION_ERROR_NO_SSL_SUPPORT:
        case PURPLE_CONNECTION_ERROR_ENCRYPTION_ERROR:
            tp_reason = TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR;
            break;
        case PURPLE_CONNECTION_ERROR_NAME_IN_USE:
            tp_reason = TP_CONNECTION_STATUS_REASON_NAME_IN_USE;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_NOT_PROVIDED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_UNTRUSTED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_EXPIRED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_EXPIRED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_NOT_ACTIVATED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_HOSTNAME_MISMATCH:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_FINGERPRINT_MISMATCH:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_SELF_SIGNED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_OTHER_ERROR:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
            break;
        default:
            g_warning ("report_disconnect_cb: "
                       "invalid PurpleDisconnectReason %u", reason);
            tp_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
    }

    tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED, tp_reason);
}
#endif

static gboolean
idle_disconnected_cb(gpointer data)
{
    PurpleAccount *account = (PurpleAccount *) data;
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);

    DEBUG ("deleting account %s", account->username);
    purple_accounts_delete (account);
    tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (conn));
    return FALSE;
}

void
disconnected_cb (PurpleConnection *pc)
{
    PurpleAccount *account = purple_connection_get_account (pc);
    TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    if(base_conn->status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
/* If we have report_disconnect_reason, then if status is not already
 * DISCONNECTED we know that it was requested.  If not, we have no idea.
 */
#if PURPLE_VERSION_CHECK(2,3,0)
            TP_CONNECTION_STATUS_REASON_REQUESTED
#else
            TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED
#endif
            );

    }

    g_idle_add(idle_disconnected_cb, account);
}

static void
_create_account (HazeConnection *self)
{
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    PurpleAccount *account;
    PurplePluginProtocolInfo *prpl_info = priv->protocol_info->prpl_info;

    g_assert (self->account == NULL);

    account = self->account =
        purple_account_new(priv->username, priv->protocol_info->prpl_id);
    purple_accounts_add (account);

    account->ui_data = self;
    purple_account_set_password (account, priv->password);

    if (priv->server && *priv->server)
    {
        GList *l;
        PurpleAccountOption *option;

        /* :'-( :'-( :'-( :'-( */
        for (l = prpl_info->protocol_options; l != NULL; l = l->next)
        {
            option = (PurpleAccountOption *)l->data;
            if (!strcmp (option->pref_name, "server") /* oscar */
                || !strcmp (option->pref_name, "connect_server")) /* xmpp */
            {
                purple_account_set_string (account, option->pref_name,
                                           priv->server);
                break;
            }
        }
        if (l == NULL)
            g_warning ("server specified, but corresponding protocol option "
                "not found!");
    }
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);

    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    base->self_handle = tp_handle_ensure (contact_handles,
        purple_account_get_username (self->account), NULL, error);
    if (!base->self_handle)
        return FALSE;

    purple_account_set_enabled(self->account, UI_ID, TRUE);
    purple_account_connect(self->account);

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    return TRUE;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    if(!self->account->disconnecting)
        purple_account_disconnect(self->account);
}

/* Must be in the same order as HazeListHandle in connection.h */
static const char *list_handle_strings[] =
{
    "subscribe",    /* HAZE_LIST_HANDLE_SUBSCRIBE */
#if 0
    "publish",      /* HAZE_LIST_HANDLE_PUBLISH */
    "hide",         /* HAZE_LIST_HANDLE_HIDE */
    "allow",        /* HAZE_LIST_HANDLE_ALLOW */
    "deny"          /* HAZE_LIST_HANDLE_DENY */
#endif
    NULL
};

static gchar*
_contact_normalize (TpHandleRepoIface *repo,
                    const gchar *id,
                    gpointer context,
                    GError **error)
{
    HazeConnection *conn = HAZE_CONNECTION (context);
    PurpleAccount *account = conn->account;
    return g_strdup (purple_normalize (account, id));
}

static void
_haze_connection_create_handle_repos (TpBaseConnection *base,
        TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
    repos[TP_HANDLE_TYPE_CONTACT] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT, _contact_normalize,
                                    base);
    /* repos[TP_HANDLE_TYPE_ROOM] = XXX MUC */
    repos[TP_HANDLE_TYPE_GROUP] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP, NULL, NULL);
    repos[TP_HANDLE_TYPE_LIST] =
        tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST, list_handle_strings);
}

static GPtrArray *
_haze_connection_create_channel_factories (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    GPtrArray *channel_factories = g_ptr_array_new ();

    self->im_factory = HAZE_IM_CHANNEL_FACTORY (
        g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->im_factory);

    self->contact_list = HAZE_CONTACT_LIST (
        g_object_new (HAZE_TYPE_CONTACT_LIST, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->contact_list);

    return channel_factories;
}

gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);

    return g_strdup (purple_account_get_username (self->account));
}

static void
haze_connection_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_USERNAME:
            g_value_set_string (value, priv->username);
            break;
        case PROP_PASSWORD:
            g_value_set_string (value, priv->password);
            break;
        case PROP_SERVER:
            g_value_set_string (value, priv->server);
            break;
        case PROP_PROTOCOL_INFO:
            g_value_set_pointer (value, priv->protocol_info);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_connection_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_USERNAME:
            g_free (priv->username);
            priv->username = g_value_dup_string(value);
            break;
        case PROP_PASSWORD:
            g_free (priv->password);
            priv->password = g_value_dup_string(value);
            break;
        case PROP_SERVER:
            g_free (priv->server);
            priv->server = g_value_dup_string(value);
            break;
        case PROP_PROTOCOL_INFO:
            priv->protocol_info = g_value_get_pointer (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static GObject *
haze_connection_constructor (GType type,
                             guint n_construct_properties,
                             GObjectConstructParam *construct_params)
{
    HazeConnection *self = HAZE_CONNECTION (
            G_OBJECT_CLASS (haze_connection_parent_class)->constructor (
                type, n_construct_properties, construct_params));

    DEBUG ("Post-construction: (HazeConnection *)%p", self);

    _create_account (self);

    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);

    DEBUG ("disposing of (HazeConnection *)%p", self);

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    g_free (priv->username);
    g_free (priv->password);
    g_free (priv->server);
    self->priv = NULL;

    tp_presence_mixin_finalize (object);

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;
    static const gchar *interfaces_always_present[] = {
        TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
        /* TODO: This is a lie.  Not all protocols supported by libpurple
         *       actually have the concept of a user-settable alias, but
         *       there's no way for the UI to know (yet).
         */
        TP_IFACE_CONNECTION_INTERFACE_ALIASING,
        NULL };

    DEBUG ("Initializing (HazeConnectionClass *)%p", klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionPrivate));
    object_class->get_property = haze_connection_get_property;
    object_class->set_property = haze_connection_set_property;
    object_class->constructor = haze_connection_constructor;
    object_class->dispose = haze_connection_dispose;
    object_class->finalize = haze_connection_finalize;

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_factories =
        _haze_connection_create_channel_factories;
    base_class->get_unique_connection_name =
        haze_connection_get_unique_connection_name;
    base_class->start_connecting = _haze_connection_start_connecting;
    base_class->shut_down = _haze_connection_shut_down;
    base_class->interfaces_always_present = interfaces_always_present;

    param_spec = g_param_spec_string ("username", "Account username",
                                      "The username used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "Account password",
                                      "The password used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

    param_spec = g_param_spec_string ("server", "Hostname or IP of server",
                                      "The server used when establishing a connection.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_SERVER, param_spec);

    param_spec = g_param_spec_pointer ("protocol-info",
                                       "HazeProtocolInfo instance",
                                       "Information on how this protocol "
                                       "should be treated by haze",
                                       G_PARAM_CONSTRUCT_ONLY |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_NAME |
                                       G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PROTOCOL_INFO,
                                     param_spec);

    haze_connection_presence_class_init (object_class);
    haze_connection_aliasing_class_init (object_class);
    haze_connection_avatars_class_init (object_class);
}

static void
haze_connection_init (HazeConnection *self)
{
    DEBUG ("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);

    haze_connection_presence_init (self);
}

/* Without the ifdef check, this compiles with warnings.  Except I want
 * -Werror, so...
 */
static void *
request_authorize_cb (PurpleAccount *account,
                      const char *remote_user,
                      const char *id,
                      const char *alias,
                      const char *message,
                      gboolean on_list,
#if PURPLE_VERSION_CHECK(2,1,1)
                      PurpleAccountRequestAuthorizationCb authorize_cb,
                      PurpleAccountRequestAuthorizationCb deny_cb,
#else
                      GCallback authorize_cb,
                      GCallback deny_cb,
#endif
                      void *user_data)
{
    /* Woo for argument lists which are longer than the function! */
    PurpleAccountRequestAuthorizationCb cb =
#if PURPLE_VERSION_CHECK(2,1,1)
        authorize_cb;
#else
        (PurpleAccountRequestAuthorizationCb) authorize_cb;
#endif

    /* FIXME: Implement the publish list, then deal with this properly. */
    DEBUG ("[%s] Quietly authorizing presence subscription from '%s'...",
             account->username, remote_user);
    cb (user_data);
    return NULL;
}

static PurpleAccountUiOps
account_ui_ops =
{
    NULL,                                            /* notify_added */
    haze_connection_presence_account_status_changed, /* status_changed */
    NULL,                                            /* request_add */
    request_authorize_cb,                            /* request_authorize */
    NULL,                                            /* close_account_request */

    NULL, /* purple_reserved1 */
    NULL, /* purple_reserved2 */
    NULL, /* purple_reserved3 */
    NULL  /* purple_reserved4 */
};

PurpleAccountUiOps *
haze_get_account_ui_ops ()
{
    return &account_ui_ops;
}

static PurpleConnectionUiOps
connection_ui_ops =
{
    NULL,            /* connect_progress */
    connected_cb,    /* connected */
    disconnected_cb, /* disconnected */
    NULL,            /* notice */
    NULL,            /* report_disconnect */
    NULL,            /* network_connected */
    NULL,            /* network_disconnected */
#if PURPLE_VERSION_CHECK(2,3,0)
    haze_report_disconnect_reason, /* report_disconnect_reason */
#else
    NULL, /* _purple_reserved0 */
#endif

    NULL, /* _purple_reserved1 */
    NULL, /* _purple_reserved2 */
    NULL  /* _purple_reserved3 */
};

PurpleConnectionUiOps *
haze_get_connection_ui_ops ()
{
    return &connection_ui_ops;
}

const gchar *
haze_connection_handle_inspect (HazeConnection *conn,
                                TpHandleType handle_type,
                                TpHandle handle)
{
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, handle_type);
    g_assert (tp_handle_is_valid (handle_repo, handle, NULL));
    return tp_handle_inspect (handle_repo, handle);
}
