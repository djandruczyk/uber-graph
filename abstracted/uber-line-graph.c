/* uber-line-graph.c
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "uber-line-graph.h"

/**
 * SECTION:uber-line-graph.h
 * @title: UberLineGraph
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberLineGraph, uber_line_graph, UBER_TYPE_GRAPH)

struct _UberLineGraphPrivate
{
	gpointer dummy;
};

/**
 * uber_line_graph_new:
 *
 * Creates a new instance of #UberLineGraph.
 *
 * Returns: the newly created instance of #UberLineGraph.
 * Side effects: None.
 */
GtkWidget*
uber_line_graph_new (void)
{
	UberLineGraph *graph;

	graph = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	return GTK_WIDGET(graph);
}

/**
 * uber_line_graph_finalize:
 * @object: A #UberLineGraph.
 *
 * Finalizer for a #UberLineGraph instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_finalize (GObject *object) /* IN */
{
	G_OBJECT_CLASS(uber_line_graph_parent_class)->finalize(object);
}

/**
 * uber_line_graph_class_init:
 * @klass: A #UberLineGraphClass.
 *
 * Initializes the #UberLineGraphClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_class_init (UberLineGraphClass *klass) /* IN */
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_line_graph_finalize;
	g_type_class_add_private(object_class, sizeof(UberLineGraphPrivate));
}

/**
 * uber_line_graph_init:
 * @graph: A #UberLineGraph.
 *
 * Initializes the newly created #UberLineGraph instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_init (UberLineGraph *graph) /* IN */
{
	UberLineGraphPrivate *priv;

	graph->priv = G_TYPE_INSTANCE_GET_PRIVATE(graph,
	                                          UBER_TYPE_LINE_GRAPH,
	                                          UberLineGraphPrivate);
	priv = graph->priv;
}
