/* uber-range.h
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

#ifndef __UBER_RANGE_H__
#define __UBER_RANGE_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * UberRange:
 *
 * #UberRange is a structure that encapsulates the range of a particular
 * scale.  It contains the beginning value, ending value, and a pre-calculated
 * range between the values.
 */
typedef struct
{
	gdouble begin;
	gdouble end;
	gdouble range;
} UberRange;

G_END_DECLS

#endif /* __UBER_RANGE_H__ */
