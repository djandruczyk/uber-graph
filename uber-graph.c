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

static void gdk_cairo_rectangle_clean        (cairo_t      *cr,
                                              GdkRectangle *rect);
static void gdk_pixmap_scale_simple          (GdkPixmap    *src,
                                              GdkPixmap    *dst);
static void pango_layout_get_pixel_rectangle (PangoLayout  *layout,
                                              GdkRectangle *rect);
static void uber_graph_calculate_rects       (UberGraph    *graph);

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

G_DEFINE_TYPE(UberGraph, uber_graph, GTK_TYPE_DRAWING_AREA)

typedef struct
{
	GdkPixmap   *bg_pixmap;
	GdkPixmap   *fg_pixmap;
	gboolean     redraw;

	cairo_t     *bg_cairo;
	cairo_t     *fg_cairo;

	PangoLayout *title_layout;
	PangoLayout *axis_layout;
	PangoLayout *tick_layout;
} GraphInfo;

struct _UberGraphPrivate
{
	GStaticRWLock  rw_lock;
	GraphInfo      info[2];  /* Two GraphInfo's for swapping. */
	gboolean       flipped;  /* Which GraphInfo is active. */
	gchar         *title;    /* Graph title. */
	gchar         *x_label;  /* Graph X-axis label. */
	gchar         *y_label;  /* Graph Y-axis label. */
	gint           tick_len; /* Length of axis ticks in pixels. */

	GdkGC         *bg_gc;    /* Drawing context for blitting background */
	GdkGC         *fg_gc;    /* Drawing context for blitting foreground */

	GdkRectangle   title_rect;
	GdkRectangle   x_label_rect;
	GdkRectangle   x_tick_rect;
	GdkRectangle   y_label_rect;
	GdkRectangle   y_tick_rect;
	GdkRectangle   content_rect;
};

enum
{
	LAYOUT_TITLE,
	LAYOUT_X_LABEL,
	LAYOUT_Y_LABEL,
	LAYOUT_X_TICK,
	LAYOUT_Y_TICK,
};

enum
{
	PROP_0,
	PROP_TITLE,
};

const GdkColor colors[] = {
	{ 0, 0xABCD, 0xABCD, 0xABCD },
	{ 0, 0xFF00, 0xFF00, 0xFF00 },
	{ 0, 0xA0C0, 0xA0C0, 0xA0C0 },
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
 * uber_graph_get_title:
 * @graph: A UberGraph.
 *
 * Retrieves the current graph title.
 *
 * Returns: The graph title string.  This value should not be modified
 *   or freed.
 * Side effects: None.
 */
const gchar *
uber_graph_get_title (UberGraph *graph) /* IN */
{
	g_return_val_if_fail(UBER_IS_GRAPH(graph), NULL);
	return graph->priv->title;
}

/**
 * uber_graph_set_title:
 * @graph: A UberGraph.
 * @title: The new title.
 *
 * Sets the title of the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_title (UberGraph   *graph, /* IN */
                      const gchar *title) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Update the title field.
	 */
	g_static_rw_lock_writer_lock(&priv->rw_lock);
	g_free(priv->title);
	priv->title = g_markup_printf_escaped("<b>%s</b>", title);
	g_static_rw_lock_writer_unlock(&priv->rw_lock);
	/*
	 * Force redraw/relayout of the graph.
	 */
	uber_graph_calculate_rects(graph);
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
uber_graph_render_thread (gpointer user_data) /* IN */
{
	g_message("ENTRY: %s():%d", G_STRFUNC, __LINE__);
	g_usleep(G_USEC_PER_SEC * 1000);
	g_message(" EXIT: %s():%d", G_STRFUNC, __LINE__);

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
 * uber_graph_prepare_layout:
 * @graph: A #UberGraph.
 * @layout: A #PangoLayout.
 * @mode: The layout mode.
 *
 * Prepares the #PangoLayout with the settings required to render the given
 * mode, such as LAYOUT_TITLE.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_prepare_layout (UberGraph   *graph,  /* IN */
                           PangoLayout *layout, /* IN */
                           gint         mode)   /* IN */
{
	UberGraphPrivate *priv;
	PangoFontDescription *desc;

	priv = graph->priv;
	desc = pango_font_description_new();
	switch (mode) {
	case LAYOUT_TITLE:
		pango_font_description_set_family(desc, "Sans");
		pango_font_description_set_size(desc, 10 * PANGO_SCALE);
		break;
	case LAYOUT_X_LABEL:
	case LAYOUT_Y_LABEL:
		pango_font_description_set_family(desc, "Sans");
		pango_font_description_set_size(desc, 8 * PANGO_SCALE);
		break;
	case LAYOUT_X_TICK:
	case LAYOUT_Y_TICK:
		pango_font_description_set_family(desc, "Sans");
		pango_font_description_set_size(desc, 6 * PANGO_SCALE);
		break;
	default:
		g_assert_not_reached();
	}
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
}

/**
 * pango_layout_get_pixel_rectangle:
 * @layout; A PangoLayout.
 * @rect: A GdkRectangle.
 *
 * Helper to retrieve the area of a layout using a GdkRectangle.
 *
 * Returns: None.
 * Side effects: None.
 */
static inline void
pango_layout_get_pixel_rectangle (PangoLayout  *layout, /* IN */
                                  GdkRectangle *rect)   /* IN */
{
	rect->x = 0;
	rect->y = 0;
	pango_layout_get_pixel_size(layout, &rect->width, &rect->height);
}

/**
 * uber_graph_calculate_rects:
 * @graph: A #UberGraph.
 *
 * Calculates the locations of various features within the graph.  The various
 * rendering methods use these calculations quickly place items in the correct
 * location.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_calculate_rects (UberGraph *graph) /* IN */

{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkWindow *window;
	PangoLayout *pl;
	cairo_t *cr;
	gint w;
	gint h;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	if (!(window = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		return;
	}
	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * Create a cairo context and PangoLayout to calculate the sizing
	 * of various strings.
	 */
	cr = gdk_cairo_create(GDK_DRAWABLE(window));
	pl = pango_cairo_create_layout(cr);
	/*
	 * Calculate the size of the title.
	 */
	uber_graph_prepare_layout(graph, pl, LAYOUT_TITLE);
	pango_layout_set_markup(pl, priv->title, -1);
	pango_layout_get_pixel_rectangle(pl, &priv->title_rect);
	priv->title_rect.x = (alloc.width / 2.) - (priv->title_rect.width / 2.);
	/*
	 * Calculate the size/position of the Y-Axis label.
	 */
	uber_graph_prepare_layout(graph, pl, LAYOUT_Y_LABEL);
	pango_layout_set_text(pl, priv->y_label, -1);
	pango_layout_get_pixel_rectangle(pl, &priv->y_label_rect);
	priv->y_label_rect.x = alloc.height - priv->y_label_rect.height;
	priv->y_label_rect.y = alloc.width - (priv->y_label_rect.width / 2.);
	/*
	 * Calculate the size/position of the X-Axis label.
	 */
	uber_graph_prepare_layout(graph, pl, LAYOUT_X_LABEL);
	pango_layout_set_text(pl, priv->x_label, -1);
	pango_layout_get_pixel_rectangle(pl, &priv->x_label_rect);
	priv->y_label_rect.y = alloc.height - (priv->x_label_rect.width / 2.);
	/*
	 * Determine largest size of tick labels.
	 */
	uber_graph_prepare_layout(graph, pl, LAYOUT_X_TICK);
	pango_layout_set_text(pl, "XXXX", -1);
	pango_layout_get_pixel_size(pl, &w, &h);
	/*
	 * Calculate the X-Axis tick area.
	 */
	priv->x_tick_rect.height = priv->tick_len + h;
	priv->x_tick_rect.x = priv->x_label_rect.width + w + priv->tick_len;
	priv->x_tick_rect.width = alloc.width - priv->x_tick_rect.x;
	priv->x_tick_rect.y = alloc.height - priv->x_label_rect.height - priv->x_tick_rect.height;
	/*
	 * Calculate the Y-Axis tick area.
	 */
	priv->y_tick_rect.height = priv->x_tick_rect.y - priv->title_rect.height;
	priv->y_tick_rect.y = priv->title_rect.height;
	priv->y_tick_rect.x = priv->x_label_rect.x + priv->x_label_rect.width;
	priv->y_tick_rect.width = w + priv->tick_len;
	/*
	 * Calculate the content region.
	 */
	priv->content_rect.x = priv->y_tick_rect.x + priv->y_tick_rect.width;
	priv->content_rect.y = priv->title_rect.y + priv->title_rect.height;
	priv->content_rect.width = alloc.width - priv->content_rect.x;
	priv->content_rect.height = priv->x_tick_rect.y - priv->content_rect.y;
	/*
	 * Cleanup after allocations.
	 */
	g_object_unref(pl);
	cairo_destroy(cr);
}

/**
 * uber_graph_render_bg_task:
 * @graph: A #UberGraph.
 * @info: A GraphInfo.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_bg_task (UberGraph *graph, /* IN */
                           GraphInfo *info)  /* IN */
{
	const gdouble dashes[] = { 1., 2. };
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkColor bg_color;
	GdkColor white;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	priv = graph->priv;
	cairo_save(info->bg_cairo);
	/*
	 * Retrieve required data for rendering.
	 */
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	bg_color = gtk_widget_get_style(GTK_WIDGET(graph))->bg[GTK_STATE_NORMAL];
	gdk_color_parse("#fff", &white);
	/*
	 * Clear the background to default color.
	 */
	cairo_rectangle(info->bg_cairo, 0, 0, alloc.width, alloc.height);
	gdk_cairo_set_source_color(info->bg_cairo, &bg_color);
	cairo_fill(info->bg_cairo);
	/*
	 * Fill in the content rectangle and stroke edge.
	 */
	gdk_cairo_rectangle_clean(info->bg_cairo, &priv->content_rect);
	cairo_set_source_rgb(info->bg_cairo, 1, 1, 1);
	cairo_fill_preserve(info->bg_cairo);
	cairo_set_dash(info->bg_cairo, dashes, G_N_ELEMENTS(dashes), 0);
	cairo_set_line_width(info->bg_cairo, 1.0);
	cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	cairo_stroke(info->bg_cairo);
	/*
	 * Render the graph title.
	 */
	cairo_move_to(info->bg_cairo, priv->title_rect.x, priv->title_rect.y);
	cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	pango_layout_set_markup(info->title_layout, priv->title, -1);
	pango_cairo_show_layout(info->bg_cairo, info->title_layout);
	/*
	 * Render the X-Axis ticks.
	 */
	gdk_cairo_rectangle_clean(info->bg_cairo, &priv->x_tick_rect);
	cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	cairo_fill(info->bg_cairo);
	/*
	 * Render the Y-Axis ticks.
	 */
	gdk_cairo_rectangle_clean(info->bg_cairo, &priv->y_tick_rect);
	cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	cairo_fill(info->bg_cairo);
	/*
	 * Cleanup.
	 */
	cairo_restore(info->bg_cairo);
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
	GdkColor bg_color;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	drawable = GDK_DRAWABLE(gtk_widget_get_window(GTK_WIDGET(graph)));
	bg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	fg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);

	/*
	 * Set background to default widget background.
	 */
	bg_color = gtk_widget_get_style(GTK_WIDGET(graph))->bg[GTK_STATE_NORMAL];
	cr = gdk_cairo_create(GDK_DRAWABLE(bg_pixmap));
	gdk_cairo_set_source_color(cr, &bg_color);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_fill(cr);
	cairo_destroy(cr);

	/*
	 * Clear contents of foreground.
	 */
	cr = gdk_cairo_create(GDK_DRAWABLE(fg_pixmap));
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_fill(cr);
	cairo_destroy(cr);

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
		if (info->title_layout) {
			g_object_unref(info->title_layout);
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
	info->title_layout = pango_cairo_create_layout(info->bg_cairo);
	info->axis_layout = pango_cairo_create_layout(info->bg_cairo);
	info->tick_layout = pango_cairo_create_layout(info->bg_cairo);

	/*
	 * Update the layouts to reflect proper styling.
	 */
	uber_graph_prepare_layout(graph, info->title_layout, LAYOUT_TITLE);
	uber_graph_prepare_layout(graph, info->axis_layout, LAYOUT_X_LABEL);
	uber_graph_prepare_layout(graph, info->tick_layout, LAYOUT_X_TICK);
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
	if (info->title_layout) {
		g_object_unref(info->title_layout);
	}
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
	uber_graph_calculate_rects(UBER_GRAPH(widget));
	g_static_rw_lock_writer_unlock(&priv->rw_lock);
}

/**
 * gdk_cairo_rectangle_clean:
 * @cr: A #cairo_t.
 * @rect: A #GdkRectangle.
 *
 * Like gdk_cairo_rectangle(), except it attempts to make sure that the
 * values are lined up according to their "half" value to make cleaner
 * lines.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
gdk_cairo_rectangle_clean (cairo_t      *cr,   /* IN */
                           GdkRectangle *rect) /* IN */
{
	gdouble x;
	gdouble y;
	gdouble w;
	gdouble h;

	x = rect->x + .5;
	y = rect->y + .5;
	w = rect->width - 1.0;
	h = rect->height - 1.0;
	cairo_rectangle(cr, x, y, w, h);
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

	priv = UBER_GRAPH(widget)->priv;
	dst = GDK_DRAWABLE(gtk_widget_get_window(widget));
	g_static_rw_lock_reader_lock(&priv->rw_lock);
	info = &priv->info[priv->flipped];
#if 1 /* Synchronous Drawing */
	uber_graph_render_bg_task(UBER_GRAPH(widget), info);
#endif
	/*
	 * Blit the background for the exposure area.
	 */
	if (G_LIKELY(info->bg_pixmap)) {
		gdk_draw_drawable(dst, priv->bg_gc, GDK_DRAWABLE(info->bg_pixmap),
		                  expose->area.x, expose->area.y,
		                  expose->area.x, expose->area.y,
		                  expose->area.width, expose->area.height);
	}
	/*
	 * Blit the foreground for the exposure area.
	 */
	if (G_LIKELY(info->fg_pixmap)) {
		gdk_draw_drawable(dst, priv->fg_gc, GDK_DRAWABLE(info->fg_pixmap),
		                  expose->area.x, expose->area.y,
		                  expose->area.x, expose->area.y,
		                  expose->area.width, expose->area.height);
	}
	g_static_rw_lock_reader_unlock(&priv->rw_lock);

	return FALSE;
}

/**
 * uber_graph_style_set:
 * @widget: A GtkWidget.
 *
 * Callback upon the changing of the active GtkStyle of @widget.  The styling
 * for the various pixmaps are updated.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_style_set (GtkWidget *widget,     /* IN */
                      GtkStyle  *old_style)  /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	BASE_CLASS->style_set(widget, old_style);
	if (!gtk_widget_get_window(widget)) {
		return;
	}
	g_static_rw_lock_writer_lock(&priv->rw_lock);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[0]);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[1]);
	g_static_rw_lock_writer_unlock(&priv->rw_lock);
}

/**
 * uber_graph_realize:
 * @widget: A #UberGraph.
 *
 * Handles the "realize" signal for the #UberGraph.  Required server side
 * contexts that are needed for the entirety of the widgets life are created.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_realize (GtkWidget *widget) /* IN */
{
	UberGraphPrivate *priv;
	GdkDrawable *dst;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	BASE_CLASS->realize(widget);
	priv = UBER_GRAPH(widget)->priv;
	dst = GDK_DRAWABLE(gtk_widget_get_window(widget));
	priv->bg_gc = gdk_gc_new(dst);
	priv->fg_gc = gdk_gc_new(dst);
	gdk_gc_set_function(priv->fg_gc, GDK_OR);
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
	if (priv->fg_gc) {
		g_object_unref(priv->fg_gc);
	}
	if (priv->bg_gc) {
		g_object_unref(priv->bg_gc);
	}
	g_free(priv->title);
	g_free(priv->x_label);
	g_free(priv->y_label);
	G_OBJECT_CLASS(uber_graph_parent_class)->finalize(object);
}

static void
uber_graph_get_property (GObject    *object,  /* IN */
                         guint       prop_id, /* IN */
                         GValue     *value,   /* OUT */
                         GParamSpec *pspec)   /* IN */
{
	UberGraph *graph = UBER_GRAPH(object);

	switch (prop_id) {
	case PROP_TITLE:
		g_value_set_string(value, uber_graph_get_title(graph));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void
uber_graph_set_property (GObject      *object,  /* IN */
                         guint         prop_id, /* IN */
                         const GValue *value,   /* IN */
                         GParamSpec   *pspec)   /* IN */
{
	UberGraph *graph = UBER_GRAPH(object);

	switch (prop_id) {
	case PROP_TITLE:
		uber_graph_set_title(graph, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
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
	object_class->set_property = uber_graph_set_property;
	object_class->get_property = uber_graph_get_property;
	g_type_class_add_private(object_class, sizeof(UberGraphPrivate));

	widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->expose_event = uber_graph_expose_event;
	widget_class->realize = uber_graph_realize;
	widget_class->size_allocate = uber_graph_size_allocate;
	widget_class->style_set = uber_graph_style_set;

	g_object_class_install_property(object_class,
	                                PROP_TITLE,
	                                g_param_spec_string("title",
	                                                    "title",
	                                                    "Title",
	                                                    NULL,
	                                                    G_PARAM_READWRITE));
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
	priv->title = g_strdup("");
	priv->x_label = g_strdup("");
	priv->y_label = g_strdup("");
	priv->tick_len = 10;

	/*
	 * Start the render thread if needed.
	 */
	if (g_once_init_enter(&initialized)) {
		g_thread_create(uber_graph_render_thread, NULL, FALSE, NULL);
		g_once_init_leave(&initialized, TRUE);
	}
}
