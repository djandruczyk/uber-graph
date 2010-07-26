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

#include <math.h>

#include "uber-line-graph.h"
#include "g-ring.h"

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
	GRing             *raw_data;
	GRing             *scaled_data;
	UberLineGraphFunc  func;
	gpointer           func_data;
	GDestroyNotify     func_notify;
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
 * uber_line_graph_get_next_data:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static gboolean
uber_line_graph_get_next_data (UberGraph *graph) /* IN */
{
	UberLineGraphPrivate *priv;
	gdouble value = 0.;
	gboolean ret = FALSE;

	g_return_val_if_fail(UBER_IS_LINE_GRAPH(graph), FALSE);

	priv = UBER_LINE_GRAPH(graph)->priv;
	g_assert(priv->raw_data);
	g_assert(priv->scaled_data);
	/*
	 * Retrieve the next data point.
	 */
	if (priv->func) {
		if (!(ret = priv->func(UBER_LINE_GRAPH(graph), &value, priv->func_data))) {
			value = -INFINITY;
		}
	}
	g_ring_append_val(priv->raw_data, value);
	/*
	 * TODO: Scale value.
	 */
	g_ring_append_val(priv->scaled_data, value);
	return ret;
}

/**
 * uber_line_graph_set_data_func:
 * @graph: A #UberLineGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_line_graph_set_data_func (UberLineGraph     *graph,     /* IN */
                               UberLineGraphFunc  func,      /* IN */
                               gpointer           user_data, /* IN */
                               GDestroyNotify     notify)    /* IN */
{
	UberLineGraphPrivate *priv;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Free existing data func if neccessary.
	 */
	if (priv->func_notify) {
		priv->func_notify(priv->func_data);
	}
	/*
	 * Store data func.
	 */
	priv->func = func;
	priv->func_data = user_data;
	priv->func_notify = notify;
}

/**
 * uber_line_graph_render:
 * @graph: A #UberGraph.
 *
 * Renders the entire data contents of the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_render (UberGraph    *graph, /* IN */
                        cairo_t      *cr,    /* IN */
                        GdkRectangle *area)  /* IN */
{
	UberLineGraphPrivate *priv;
	gdouble value;
	gdouble each;
	gint i;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = UBER_LINE_GRAPH(graph)->priv;
	each = area->width / ((gfloat)priv->raw_data->len - 1);
	cairo_new_path(cr);
	for (i = 0; i < priv->raw_data->len; i++) {
		value = g_ring_get_index(priv->raw_data, gdouble, i);
		if (value == -INFINITY) {
			break;
		}
		cairo_line_to(cr,
		              area->x + area->width - (each * i),
		              area->y + area->height - value);
	}
	cairo_set_source_rgb(cr, 0, 0, 1.);
	cairo_stroke(cr);
}

/**
 * uber_line_graph_set_stride:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_line_graph_set_stride (UberGraph *graph, /* IN */
                            guint      dps)   /* IN */
{
	UberLineGraphPrivate *priv;
	gdouble val = -INFINITY;
	gint i;

	g_return_if_fail(UBER_IS_LINE_GRAPH(graph));

	priv = UBER_LINE_GRAPH(graph)->priv;
	/*
	 * Cleanup existing resources.
	 */
	if (priv->raw_data) {
		g_ring_unref(priv->raw_data);
	}
	if (priv->scaled_data) {
		g_ring_unref(priv->scaled_data);
	}
	/*
	 * Create new ring buffers.
	 */
	priv->raw_data = g_ring_sized_new(sizeof(gdouble), dps, NULL);
	priv->scaled_data = g_ring_sized_new(sizeof(gdouble), dps, NULL);
	/*
	 * Set default data to -INFINITY.
	 */
	for (i = 0; i < dps; i++) {
		g_ring_append_val(priv->raw_data, val);
		g_ring_append_val(priv->scaled_data, val);
	}
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
	UberLineGraphPrivate *priv;

	priv = UBER_LINE_GRAPH(object)->priv;
	g_ring_unref(priv->raw_data);
	g_ring_unref(priv->scaled_data);

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
	UberGraphClass *graph_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_line_graph_finalize;
	g_type_class_add_private(object_class, sizeof(UberLineGraphPrivate));

	graph_class = UBER_GRAPH_CLASS(klass);
	graph_class->get_next_data = uber_line_graph_get_next_data;
	graph_class->render = uber_line_graph_render;
	graph_class->set_stride = uber_line_graph_set_stride;
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

	/*
	 * Keep pointer to private data.
	 */
	graph->priv = G_TYPE_INSTANCE_GET_PRIVATE(graph,
	                                          UBER_TYPE_LINE_GRAPH,
	                                          UberLineGraphPrivate);
	priv = graph->priv;
}
