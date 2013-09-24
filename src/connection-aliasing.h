#ifndef __HAZE_CONNECTION_ALIASING_H__
#define __HAZE_CONNECTION_ALIASING_H__

/*
 * connection-aliasing.h - Aliasing interface headers of HazeConnection
 * Copyright (C) 2007 Will Thompson
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


#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

void haze_connection_aliasing_iface_init (gpointer g_iface,
    gpointer iface_data);
void haze_connection_aliasing_class_init (GObjectClass *object_class);
void haze_connection_aliasing_init (GObject *object);

extern TpDBusPropertiesMixinPropImpl *haze_connection_aliasing_properties;
void haze_connection_aliasing_properties_getter (GObject *object,
    GQuark interface, GQuark name, GValue *value, gpointer getter_data);

#endif
