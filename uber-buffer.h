/* uber-buffer.h
 *
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __UBER_BUFFER_H__
#define __UBER_BUFFER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define UBER_TYPE_BUFFER (uber_buffer_get_type())

typedef struct _UberBuffer UberBuffer;

typedef gboolean (*UberBufferForeach) (UberBuffer *buffer,
                                       gdouble     value,
                                       gpointer    user_data);

struct _UberBuffer
{
	gdouble *buffer;
	gint     len;
	gint     pos;

	/*< private >*/
	volatile gint ref_count;
};

GType       uber_buffer_get_type (void) G_GNUC_CONST;
UberBuffer* uber_buffer_new      (void);
UberBuffer* uber_buffer_ref      (UberBuffer        *buffer);
void        uber_buffer_unref    (UberBuffer        *buffer);
void        uber_buffer_set_size (UberBuffer        *buffer,
                                  gint               size);
void        uber_buffer_foreach  (UberBuffer        *buffer,
                                  UberBufferForeach  func,
                                  gpointer           user_data);
void        uber_buffer_append   (UberBuffer        *buffer,
                                  gdouble            value);

G_END_DECLS

#endif /* __UBER_BUFFER_H__ */

