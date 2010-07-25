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

#include <clutter/clutter.h>

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

struct _UberGraph
{
	ClutterActor parent;

	/*< private >*/
	UberGraphPrivate *priv;
};

struct _UberGraphClass
{
	ClutterActorClass parent_class;
};

GType         uber_graph_get_type (void) G_GNUC_CONST;
ClutterActor* uber_graph_new      (void);

G_END_DECLS

#endif /* __UBER_GRAPH_H__ */
