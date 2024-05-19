/*
 * conversation-ui.c
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
#include "conversation-ui.h"
#include "im-channel-factory.h"
#include "mu-channel-manager.h"
#include "debug.h"

static void _create_conversation(PurpleConversation *conv)
{
    if (conv->type == PURPLE_CONV_TYPE_IM)
        haze_im_create_conversation (conv);
    else if (conv->type == PURPLE_CONV_TYPE_CHAT)
        haze_mu_create_conversation (conv);
    else
        DEBUG ("Unknown conversation %d, ignoring", conv->type);
}

static PurpleConversationUiOps
conversation_ui_ops =
{
    _create_conversation,      /* create_conversation */
    NULL,                      /* destroy_conversation */
    NULL,                      /* write_chat */
    NULL,                      /* write_im */
    NULL,                      /* write_conv */
    NULL,                      /* chat_add_users */
    NULL,                      /* chat_rename_user */
    NULL,                      /* chat_remove_users */
    NULL,                      /* chat_update_user */

    NULL,                      /* present */

    NULL,                      /* has_focus */

    NULL,                      /* custom_smiley_add */
    NULL,                      /* custom_smiley_write */
    NULL,                      /* custom_smiley_close */

    NULL,                      /* send_confirm */

    NULL,                      /* _purple_reserved1 */
    NULL,                      /* _purple_reserved2 */
    NULL,                      /* _purple_reserved3 */
    NULL,                      /* _purple_reserved4 */
};


PurpleConversationUiOps *
haze_get_conversation_ui_ops(void)
{
    return &conversation_ui_ops;
}
