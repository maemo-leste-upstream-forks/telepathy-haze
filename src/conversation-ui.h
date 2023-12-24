/*
 * conversation-ui.h
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

#ifndef __HAZE_CONVERSATION_UI_H__
#define __HAZE_CONVERSATION_UI_H__

#include <libpurple/conversation.h>
#include <telepathy-glib/handle.h>

G_BEGIN_DECLS

typedef struct _HazeConversationUiData HazeConversationUiData;

struct _HazeConversationUiData
{
    TpHandle contact_handle;

    PurpleTypingState active_state;
    guint resend_typing_timeout_id;
};

#define PURPLE_CONV_GET_HAZE_UI_DATA(conv) \
    ((HazeConversationUiData *) conv->ui_data)

PurpleConversationUiOps *haze_get_conversation_ui_ops (void);

void haze_set_conversation_ui_chat_create_conversation (
    void (*cb)(PurpleConversation *conv));

void haze_set_conversation_ui_im_create_conversation (
    void (*cb)(PurpleConversation *conv));

void haze_set_conversation_ui_chat_destroy_conversation (
    void (*cb)(PurpleConversation *conv));

void haze_set_conversation_ui_im_destroy_conversation (
    void (*cb)(PurpleConversation *conv));

void haze_set_conversation_ui_im_write_conv (
    void (*cb)(PurpleConversation *conv, const char *name, const char *alias,
    const char *message, PurpleMessageFlags flags, time_t mtime));

void haze_set_conversation_ui_chat_write_conv (
    void (*cb)(PurpleConversation *conv, const char *name, const char *alias,
    const char *message, PurpleMessageFlags flags, time_t mtime));

G_END_DECLS

#endif /* __HAZE_CONVERSATION_UI_H__ */
