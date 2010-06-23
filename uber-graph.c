/* uber-graph.c
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

#include "uber-graph.h"

#define BASE_CLASS (GTK_WIDGET_CLASS(uber_graph_parent_class))

G_DEFINE_TYPE(UberGraph, uber_graph, GTK_TYPE_DRAWING_AREA)

/**
 * SECTION:uber-graph
 * @title: UberGraph
 * @short_description: Realtime, side-scrolling graph that is low on CPU.
 *
 * #UberGraph is a graphing widget that provides live scrolling features
 * for realtime graphs.  It uses server-side pixmaps to reduce the rendering
 * overhead.  Multiple pixmaps are used which can be blitted for additional
 * speed boost.
 */

typedef struct
{
	GdkPixmap   *bg_pixmap;
	GdkPixmap   *fg_pixmap;
	gboolean     redraw;

	cairo_t     *bg_cairo;
	cairo_t     *fg_cairo;

	PangoLayout *axis_layout;
	PangoLayout *tick_layout;
} GraphInfo;

struct _UberGraphPrivate
{
	GStaticRWLock  rw_lock;
	GraphInfo      info[2];  /* Two GraphInfo's for swapping. */
	gboolean       flipped;  /* Which GraphInfo is active. */
	gchar         *x_label;  /* Graph X-axis label. */
	gchar         *y_label;  /* Graph Y-axis label. */
	gint           tick_len; /* Length of axis ticks in pixels. */
};

/**
 * uber_graph_new:
 *
 * Creates a new instance of #UberGraph.
 *
 * Returns: the newly created instance of #UberGraph.
 * Side effects: None.
 */
GtkWidget*
uber_graph_new (void)
{
	UberGraph *graph;

	graph = g_object_new(UBER_TYPE_GRAPH, NULL);
	return GTK_WIDGET(graph);
}

/**
 * uber_graph_render_thread:
 * @graph: A #UberGraph.
 *
 * Render thread that updates portions of the various pixmaps as needed
 * so that they may be blitted to the screen in the main thread.
 *
 * Returns: None.
 * Side effects: Everything.
 */
static gpointer
uber_graph_render_thread (gpointer user_data)
{
	g_usleep(G_USEC_PER_SEC * 1000);
	return NULL;
}

/**
 * gdk_pixmap_scale_simple:
 * @src: A #GdkPixmap.
 * @dst: A #GdkPixmap.
 *
 * Scales the contents of @src into @dst.  This is done by retrieving the
 * server-side pixmap, converting it to a pixbuf, and then pushing the scaled
 * image back to the server.
 *
 * If you find a faster way to do this, implement it!
 *
 * Returns: None.
 * Side effects: None.
 */
static void
gdk_pixmap_scale_simple (GdkPixmap *src, /* IN */
                         GdkPixmap *dst) /* IN */
{
	GdkPixbuf *pixbuf;
	gint src_h;
	gint src_w;
	gint dst_h;
	gint dst_w;

	g_return_if_fail(src != NULL);
	g_return_if_fail(dst != NULL);

	gdk_drawable_get_size(GDK_DRAWABLE(src), &src_w, &src_h);
	gdk_drawable_get_size(GDK_DRAWABLE(src), &dst_w, &dst_h);
	pixbuf = gdk_pixbuf_get_from_drawable(NULL, GDK_DRAWABLE(src),
	                                      NULL, 0, 0, 0, 0, src_w, src_h);
	gdk_pixbuf_scale_simple(pixbuf, dst_w, dst_h, GDK_INTERP_BILINEAR);
	gdk_draw_pixbuf(GDK_DRAWABLE(dst), NULL, pixbuf, 0, 0, 0, 0, dst_w, dst_h,
	                GDK_RGB_DITHER_NORMAL, 0, 0);
	g_object_unref(pixbuf);
}

/**
 * uber_graph_init_graph_info:
 * @graph: A #UberGraph.
 * @info: A GraphInfo.
 *
 * Initializes the GraphInfo structure to match the current settings of the
 * #UberGraph.  If @info has existing server-side pixmaps, they will be scaled
 * to match the new size of the widget.
 *
 * The renderer will perform a redraw of the entire area on its next pass as
 * the contents will potentially be lossy and skewed.  But this is still far
 * better than incurring the wrath of layout rendering in the GUI thread.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_init_graph_info (UberGraph *graph, /* IN */
                            GraphInfo *info)  /* IN/OUT */
{
	GtkAllocation alloc;
	GdkDrawable *drawable;
	GdkPixmap *bg_pixmap;
	GdkPixmap *fg_pixmap;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	drawable = GDK_DRAWABLE(gtk_widget_get_window(GTK_WIDGET(graph)));
	bg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	fg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);

	/*
	 * Cleanup after any previous cairo contexts.
	 */
	if (info->bg_cairo) {
		if (info->axis_layout) {
			g_object_unref(info->axis_layout);
		}
		if (info->tick_layout) {
			g_object_unref(info->tick_layout);
		}
		cairo_destroy(info->bg_cairo);
	}
	if (info->fg_cairo) {
		cairo_destroy(info->fg_cairo);
	}

	/*
	 * If there is an existing pixmap, we will scale it to the new size
	 * so that there is data to render until the render thread has had
	 * a chance to pass over and re-render the updated content.
	 */
	if (info->bg_pixmap) {
		gdk_pixmap_scale_simple(info->bg_pixmap, bg_pixmap);
		g_object_unref(info->bg_pixmap);
	}
	if (info->fg_pixmap) {
		gdk_pixmap_scale_simple(info->fg_pixmap, fg_pixmap);
		g_object_unref(info->fg_pixmap);
	}

	info->bg_pixmap = bg_pixmap;
	info->fg_pixmap = fg_pixmap;
	info->redraw = TRUE;

	/*
	 * Update cached cairo contexts.
	 */
	info->bg_cairo = gdk_cairo_create(GDK_DRAWABLE(info->bg_pixmap));
	info->fg_cairo = gdk_cairo_create(GDK_DRAWABLE(info->fg_pixmap));

	/*
	 * Create PangoLayouts for rendering text.
	 */
	info->axis_layout = pango_cairo_create_layout(info->bg_cairo);
	info->tick_layout = pango_cairo_create_layout(info->bg_cairo);
}

/**
 * uber_graph_destroy_graph_info:
 * @graph: A #UberGraph.
 * @info: A GraphInfo.
 *
 * Cleans up and frees resources allocated to the GraphInfo.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_destroy_graph_info (UberGraph *graph, /* IN */
                               GraphInfo *info)  /* IN */
{
	if (info->axis_layout) {
		g_object_unref(info->axis_layout);
	}
	if (info->tick_layout) {
		g_object_unref(info->tick_layout);
	}
	if (info->bg_cairo) {
		cairo_destroy(info->bg_cairo);
	}
	if (info->fg_cairo) {
		cairo_destroy(info->fg_cairo);
	}
	if (info->bg_pixmap) {
		g_object_unref(info->bg_pixmap);
	}
	if (info->fg_pixmap) {
		g_object_unref(info->fg_pixmap);
	}
}

/**
 * uber_graph_size_allocate:
 * @widget: A GtkWidget.
 *
 * Handles the "size-allocate" event.  Pixmaps are re-initialized
 * and rendering can proceed.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_size_allocate (GtkWidget     *widget, /* IN */
                          GtkAllocation *alloc)  /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	BASE_CLASS->size_allocate(widget, alloc);
	priv = UBER_GRAPH(widget)->priv;
	/*
	 * Adjust the sizing of the blit pixmaps.
	 */
	g_static_rw_lock_writer_lock(&priv->rw_lock);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[0]);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[1]);
	g_static_rw_lock_writer_unlock(&priv->rw_lock);
}

/**
 * uber_graph_expose_event:
 * @widget: A #UberGraph.
 * @expose: A #GdkEventExpose.
 *
 * Handles the "expose-event" for the GtkWidget.  The current server-side
 * pixmaps are blitted as necessary.
 *
 * Returns: %TRUE if handler chain should stop; otherwise %FALSE.
 * Side effects: None.
 */
static gboolean
uber_graph_expose_event (GtkWidget      *widget, /* IN */
                         GdkEventExpose *expose) /* IN */
{
	UberGraphPrivate *priv;
	GdkDrawable *dst;
	GraphInfo *info;
	GdkGC *gc;

	priv = UBER_GRAPH(widget)->priv;
	dst = GDK_DRAWABLE(gtk_widget_get_window(widget));
	gc = gdk_gc_new(GDK_DRAWABLE(dst));
	g_static_rw_lock_reader_lock(&priv->rw_lock);
	info = &priv->info[priv->flipped];
	/*
	 * Blit the background for the exposure area.
	 */
	if (G_LIKELY(info->bg_pixmap)) {
		gdk_draw_drawable(dst, gc, GDK_DRAWABLE(info->bg_pixmap),
		                  expose->area.x, expose->area.y,
		                  expose->area.x, expose->area.y,
		                  expose->area.width, expose->area.height);
	}
	/*
	 * Blit the foreground for the exposure area.
	 */
	if (G_LIKELY(info->fg_pixmap)) {
		gdk_draw_drawable(dst, gc, GDK_DRAWABLE(info->fg_pixmap),
		                  expose->area.x, expose->area.y,
		                  expose->area.x, expose->area.y,
		                  expose->area.width, expose->area.height);
	}
	g_static_rw_lock_reader_unlock(&priv->rw_lock);
	g_object_unref(gc);
	return FALSE;
}

/**
 * uber_graph_finalize:
 * @object: A #UberGraph.
 *
 * Finalizer for a #UberGraph instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_finalize (GObject *object) /* IN */
{
	UberGraphPrivate *priv;

	priv = UBER_GRAPH(object)->priv;
	uber_graph_destroy_graph_info(UBER_GRAPH(object), &priv->info[0]);
	uber_graph_destroy_graph_info(UBER_GRAPH(object), &priv->info[1]);
	G_OBJECT_CLASS(uber_graph_parent_class)->finalize(object);
}

/**
 * uber_graph_class_init:
 * @klass: A #UberGraphClass.
 *
 * Initializes the #UberGraphClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_class_init (UberGraphClass *klass) /* IN */
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_graph_finalize;
	g_type_class_add_private(object_class, sizeof(UberGraphPrivate));

	widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->size_allocate = uber_graph_size_allocate;
	widget_class->expose_event = uber_graph_expose_event;
}

/**
 * uber_graph_init:
 * @graph: A #UberGraph.
 *
 * Initializes the newly created #UberGraph instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_init (UberGraph *graph) /* IN */
{
	static gsize initialized = FALSE;
	UberGraphPrivate *priv;

	#define GET_PRIVATE G_TYPE_INSTANCE_GET_PRIVATE
	graph->priv = GET_PRIVATE(graph, UBER_TYPE_GRAPH, UberGraphPrivate);
	priv = graph->priv;

	g_static_rw_lock_init(&priv->rw_lock);

	/*
	 * Start the render thread if needed.
	 */
	if (g_once_init_enter(&initialized)) {
		g_thread_create(uber_graph_render_thread, NULL, FALSE, NULL);
		g_once_init_leave(&initialized, TRUE);
	}
}
