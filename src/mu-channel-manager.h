/*
 * mu-channel-manager.h - HazeMuChannelManager header
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

#ifndef __HAZE_MU_CHANNEL_MANAGER_H__
#define __HAZE_MU_CHANNEL_MANAGER_H__

#include <glib-object.h>

#include <libpurple/conversation.h>

G_BEGIN_DECLS

#define HAZE_TYPE_MU_CHANNEL_MANAGER \
    (haze_mu_channel_manager_get_type())
#define HAZE_MU_CHANNEL_MANAGER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_MU_CHANNEL_MANAGER, \
                                HazeMuChannelManager))
#define HAZE_MU_CHANNEL_MANAGER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_MU_CHANNEL_MANAGER,GObject))
#define HAZE_IS_MU_CHANNEL_MANAGER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_MU_CHANNEL_MANAGER))
#define HAZE_IS_MU_CHANNEL_MANAGER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_MU_CHANNEL_MANAGER))
#define HAZE_MU_CHANNEL_MANAGER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), HAZE_TYPE_MU_CHANNEL_MANAGER, \
                               HazeMuChannelManagerClass))

typedef struct _HazeMuChannelManager      HazeMuChannelManager;
typedef struct _HazeMuChannelManagerClass HazeMuChannelManagerClass;
typedef struct _HazeMuChannelManagerPrivate HazeMuChannelManagerPrivate;

struct _HazeMuChannelManager {
    GObject parent;
    HazeMuChannelManagerPrivate *priv;
};

struct _HazeMuChannelManagerClass {
    GObjectClass parent_class;
};

GType haze_mu_channel_manager_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __HAZE_MU_CHANNEL_MANAGER_H__ */
