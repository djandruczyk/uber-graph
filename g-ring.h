/* g-ring.h
 *
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __G_RING_H__
#define __G_RING_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define g_ring_append_val(ring, val) g_ring_append_vals(ring, &(val), 1)
#define g_ring_get_index(ring, type, i)                               \
    (((type*)(ring)->data)[(((gint)(ring)->pos - 1 - (i)) >= 0) ?     \
                            ((ring)->pos - 1 - (i)) :                 \
                            ((ring)->len + ((ring)->pos - 1 - (i)))])

typedef struct _GRing GRing;

struct _GRing
{
	guint8 *data;
	guint   len;
	guint   pos;
};

GType  g_ring_get_type    (void) G_GNUC_CONST;
GRing* g_ring_sized_new   (guint           element_size,
                           guint           reserved_size,
                           GDestroyNotify  element_destroy);
void   g_ring_append_vals (GRing          *ring,
                           gconstpointer   data,
                           guint           len);
void   g_ring_foreach     (GRing          *ring,
                           GFunc           func,
                           gpointer        user_data);
GRing* g_ring_ref         (GRing          *ring);
void   g_ring_unref       (GRing          *ring);

G_END_DECLS

#endif /* __G_RING_H__ */
