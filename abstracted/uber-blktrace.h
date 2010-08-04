/* uber-blktrace.h
 *
 * Copyright (C) 2010 Andy Isaacson
 *               2010 Christian Hergert <chris@dronelabs.com>
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

#ifndef __UBER_BLKTRACE_H__
#define __UBER_BLKTRACE_H__

#include "uber-heat-map.h"

G_BEGIN_DECLS

void     uber_blktrace_init     (void);
void     uber_blktrace_next     (void);
gboolean uber_blktrace_get      (UberHeatMap  *map,
                                 GArray      **values,
                                 gpointer      user_data);
void     uber_blktrace_shutdown (void);

G_END_DECLS

#endif /* __UBER_BLKTRACE_H__ */
