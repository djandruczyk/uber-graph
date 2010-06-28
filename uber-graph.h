/* uber-graph.h
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

#ifndef __UBER_GRAPH_H__
#define __UBER_GRAPH_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define UBER_TYPE_GRAPH            (uber_graph_get_type())
#define UBER_GRAPH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), UBER_TYPE_GRAPH, UberGraph))
#define UBER_GRAPH_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), UBER_TYPE_GRAPH, UberGraph const))
#define UBER_GRAPH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  UBER_TYPE_GRAPH, UberGraphClass))
#define UBER_IS_GRAPH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UBER_TYPE_GRAPH))
#define UBER_IS_GRAPH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  UBER_TYPE_GRAPH))
#define UBER_GRAPH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  UBER_TYPE_GRAPH, UberGraphClass))

typedef struct _UberGraph        UberGraph;
typedef struct _UberGraphClass   UberGraphClass;
typedef struct _UberGraphPrivate UberGraphPrivate;
typedef struct _UberRange        UberRange;

typedef gboolean (*UberScale) (UberGraph       *graph,
                               const UberRange *values,
                               const UberRange *pixels,
                               gdouble         *value);

struct _UberRange
{
	gdouble begin;
	gdouble end;
	gdouble range;
};

struct _UberGraph
{
	GtkDrawingArea parent;

	/*< private >*/
	UberGraphPrivate *priv;
};

struct _UberGraphClass
{
	GtkDrawingAreaClass parent_class;
};

GType          uber_graph_get_type        (void) G_GNUC_CONST;
GtkWidget*     uber_graph_new             (void);
void           uber_graph_push            (UberGraph       *graph,
                                           gdouble          value);
void           uber_graph_set_fps         (UberGraph       *graph,
                                           gint             fps);
void           uber_graph_set_yrange      (UberGraph       *graph,
                                           const UberRange *range);
void           uber_graph_set_scale       (UberGraph       *graph,
                                           UberScale        scale);
void           uber_graph_set_yautoscale  (UberGraph       *graph,
                                           gboolean         yautoscale);
gboolean       uber_graph_get_yautoscale  (UberGraph       *graph);
void           uber_graph_set_stride      (UberGraph       *graph,
                                           gint             stride);
gboolean       uber_scale_linear          (UberGraph       *graph,
                                           const UberRange *values,
                                           const UberRange *pixels,
                                           gdouble         *value);

G_END_DECLS

#endif /* __UBER_GRAPH_H__ */
