/*
 * mu-channel.h - HazeMuChannel header
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

#ifndef __HAZE__MU_CHANNEL_H__
#define __HAZE__MU_CHANNEL_H__

#include <telepathy-glib/message-mixin.h>
#include <telepathy-glib/group-mixin.h>

#include <libpurple/conversation.h>

G_BEGIN_DECLS

typedef struct _HazeMUChannel HazeMUChannel;
typedef struct _HazeMUChannelPrivate HazeMUChannelPrivate;
typedef struct _HazeMUChannelClass HazeMUChannelClass;


struct _HazeMUChannelClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass properties_class;
    TpGroupMixinClass group_class;
};

struct _HazeMUChannel {
    GObject parent;

    TpMessageMixin messages;
    TpGroupMixin group;

    HazeMUChannelPrivate *priv;
};

GType haze_mu_channel_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_MU_CHANNEL \
  (haze_mu_channel_get_type ())
#define HAZE_MU_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_MU_CHANNEL, \
                              HazeMUChannel))
#define HAZE_MU_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_MU_CHANNEL, \
                           HazeMUChannelClass))
#define HAZE_IS_MU_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_MU_CHANNEL))
#define HAZE_IS_MU_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_MU_CHANNEL))
#define HAZE_MU_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_MU_CHANNEL, \
                              HazeMUChannelClass))

void haze_mu_channel_start (HazeMUChannel *self);

void haze_mu_channel_receive (HazeMUChannel *self, const char *who,
    const char *xhtml_message, PurpleMessageFlags flags, time_t mtime);

void haze_mu_channel_add_users (HazeMUChannel *self, GList *cbuddies,
    gboolean new_arrivals);

void haze_mu_channel_remove_users(HazeMUChannel *self, GList *users);

G_END_DECLS

#endif /* __HAZE__MU_CHANNEL_H__ */
