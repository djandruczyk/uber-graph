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

#include <math.h>

#include "uber-graph.h"
#include "uber-buffer.h"

#define BASE_CLASS   (GTK_WIDGET_CLASS(uber_graph_parent_class))
#define DEFAULT_SIZE (64)
#define GET_PRIVATE  G_TYPE_INSTANCE_GET_PRIVATE

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

	PangoLayout *tick_layout;
} GraphInfo;

struct _UberGraphPrivate
{
	GraphInfo      info[2];  /* Two GraphInfo's for swapping. */
	gboolean       flipped;  /* Which GraphInfo is active. */
	gint           tick_len; /* Length of axis ticks in pixels. */
	gint           fps;      /* Frames per second. */
	gint           fps_off;  /* Offset in frame-slide */
	gint           fps_to;   /* Frames per second timeout (in MS) */
	gfloat         fps_each; /* How much each frame skews. */
	guint          fps_handler; /* GSource identifier for invalidating rect. */
	UberScale      scale;    /* Scaling of values to pixels. */
	UberRange      yrange;
	UberBuffer    *buffer;   /* Circular buffer of raw values. */
	UberBuffer    *scaled;   /* Circular buffer of scaled values. */
	gboolean       bg_dirty; /* Do we need to update the background. */

	GdkGC         *bg_gc;    /* Drawing context for blitting background */
	GdkGC         *fg_gc;    /* Drawing context for blitting foreground */

	GdkRectangle   x_tick_rect;
	GdkRectangle   y_tick_rect;
	GdkRectangle   content_rect;
};

enum
{
	LAYOUT_TICK,
};

const GdkColor colors[] = {
	{ 0, 0xABCD, 0xABCD, 0xABCD },
	{ 0, 0xFF00, 0xFF00, 0xFF00 },
	{ 0, 0xA0C0, 0xA0C0, 0xA0C0 },
};

static void gdk_cairo_rectangle_clean        (cairo_t      *cr,
                                              GdkRectangle *rect);
static void gdk_pixmap_scale_simple          (GdkPixmap    *src,
                                              GdkPixmap    *dst);
static void pango_layout_get_pixel_rectangle (PangoLayout  *layout,
                                              GdkRectangle *rect);
static void uber_graph_calculate_rects       (UberGraph    *graph);
static void uber_graph_init_graph_info       (UberGraph    *graph,
                                              GraphInfo    *info);

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
 * uber_graph_set_scale:
 * @graph: An #UberGraph.
 * @scale: The scale function.
 *
 * Sets the transformation scale from input values to pixels.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_scale (UberGraph *graph, /* IN */
                      UberScale  scale) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(scale != NULL);

	priv = graph->priv;
	priv->scale = scale;
	uber_graph_init_graph_info(graph, &priv->info[0]);
	uber_graph_init_graph_info(graph, &priv->info[1]);
	uber_graph_calculate_rects(graph);
}

/**
 * uber_graph_push:
 * @graph: A #UberGraph.
 *
 * Pushes a new value onto the graph. The value is translated by the scale
 * before being rendered.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_push (UberGraph *graph, /* IN */
                 gdouble    value) /* IN */
{
	UberGraphPrivate *priv;
	GdkWindow *window;
	UberRange pixel_range;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->fps_off = 0;
	uber_buffer_append(priv->buffer, value);
	/*
	 * Scale value and cache.
	 */
	pixel_range.begin = priv->content_rect.y;
	pixel_range.end = pixel_range.begin + priv->content_rect.height;
	pixel_range.range = priv->content_rect.height;
	priv->scale(graph, &priv->yrange, &pixel_range, &value);
	uber_buffer_append(priv->scaled, value);
	/*
	 * Invalidate regions and render.
	 */
	window = gtk_widget_get_window(GTK_WIDGET(graph));
	gdk_window_invalidate_rect(window, &priv->content_rect, FALSE);
}

/**
 * uber_graph_set_stride:
 * @graph: A UberGraph.
 *
 * Sets the number of x-axis points allowed in the circular buffer.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_stride (UberGraph *graph, /* IN */
                       gint       stride) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(stride > 0);

	priv = graph->priv;
	uber_buffer_set_size(priv->buffer, stride);
	uber_buffer_set_size(priv->scaled, stride);
	uber_graph_init_graph_info(graph, &priv->info[0]);
	uber_graph_init_graph_info(graph, &priv->info[1]);
	uber_graph_calculate_rects(graph);
}

/**
 * uber_graph_set_yrange:
 * @graph: A UberGraph.
 * @range: An #UberRange.
 *
 * Sets the vertical range the graph should contain.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_yrange (UberGraph       *graph,  /* IN */
                       const UberRange *yrange) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->yrange = *yrange;
	if (priv->yrange.range == 0.) {
		priv->yrange.range = priv->yrange.end - priv->yrange.begin;
	}
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

static gboolean
uber_graph_fps_timeout (gpointer data) /* IN */
{
	UberGraph *graph = data;
	GdkWindow *window;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	window = gtk_widget_get_window(GTK_WIDGET(graph));
	gdk_window_invalidate_rect(window, &graph->priv->content_rect, FALSE);
	return TRUE;
}

/**
 * uber_graph_set_fps:
 * @graph: A UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_fps (UberGraph *graph, /* IN */
                    gint       fps)   /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(fps > 0 && fps <= 60);

	priv = graph->priv;
	priv->fps = fps;
	priv->fps_to = 1000 / fps;
	priv->fps_each = (gfloat)priv->content_rect.width / (gfloat)priv->buffer->len / (gfloat)priv->fps;
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	priv->fps_handler = g_timeout_add(priv->fps_to,
	                                  uber_graph_fps_timeout,
	                                  graph);
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
	case LAYOUT_TICK:
		pango_font_description_set_family(desc, "MONOSPACE");
		pango_font_description_set_size(desc, 8 * PANGO_SCALE);
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
	gint tick_w;
	gint tick_h;

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
	 * Determine largest size of tick labels.
	 */
	uber_graph_prepare_layout(graph, pl, LAYOUT_TICK);
	pango_layout_set_text(pl, "XXXX", -1);
	pango_layout_get_pixel_size(pl, &tick_w, &tick_h);
	/*
	 * Calculate the X-Axis tick area.
	 */
	priv->x_tick_rect.height = priv->tick_len + tick_w;
	priv->x_tick_rect.x = priv->tick_len + tick_w;
	priv->x_tick_rect.width = alloc.width - priv->x_tick_rect.x;
	priv->x_tick_rect.y = alloc.height - priv->x_tick_rect.height;
	/*
	 * Calculate the Y-Axis tick area.
	 */
	priv->y_tick_rect.x = 0;
	priv->y_tick_rect.y = 0;
	priv->y_tick_rect.width = tick_w + priv->tick_len;
	priv->y_tick_rect.height = priv->x_tick_rect.y;
	/*
	 * Calculate the content region.
	 */
	priv->content_rect.x = priv->y_tick_rect.x + priv->y_tick_rect.width;
	priv->content_rect.y = 0;
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
	 * Render the X-Axis ticks.
	 */
	//gdk_cairo_rectangle_clean(info->bg_cairo, &priv->x_tick_rect);
	//cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	//cairo_fill(info->bg_cairo);
	/*
	 * Render the Y-Axis ticks.
	 */
	//gdk_cairo_rectangle_clean(info->bg_cairo, &priv->y_tick_rect);
	//cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	//cairo_fill(info->bg_cairo);
	/*
	 * Cleanup.
	 */
	cairo_restore(info->bg_cairo);
}

typedef struct
{
	UberGraph *graph;
	GraphInfo *info;
	UberScale  scale;
	UberRange  pixel_range;
	UberRange  value_range;
	gdouble    last_y;
	gdouble    last_x;
	gdouble    x_epoch;
	gdouble    each;
	gint       offset;
	gboolean   first;
} RenderClosure;

static inline gboolean
uber_graph_render_fg_each (UberBuffer *buffer,    /* IN */
                           gdouble     value,     /* IN */
                           gpointer    user_data) /* IN */
{
	RenderClosure *closure = user_data;
	gdouble y;
	gdouble x;

	g_assert(closure->graph->priv->scale != NULL);

	x = closure->x_epoch - (closure->offset++ * closure->each);
	if (value == -INFINITY) {
		goto skip;
	}
	y = closure->pixel_range.end - value;
	if (G_UNLIKELY(closure->first)) {
		closure->first = FALSE;
		cairo_move_to(closure->info->fg_cairo, x, y);
		goto finish;
	}
	cairo_curve_to(closure->info->fg_cairo,
	               closure->last_x - (closure->each / 2.),
	               closure->last_y,
	               closure->last_x - (closure->each / 2.),
	               y,
	               x, y);
	goto finish;
  skip:
	cairo_move_to(closure->info->fg_cairo, x,
	              closure->pixel_range.end);
  finish:
  	closure->last_x = x;
  	closure->last_y = y;
	return FALSE;
}

static inline gboolean
gdk_color_parse_xor (GdkColor    *color, /* IN */
                     const gchar *spec,  /* IN */
                     gint         xor)   /* IN */
{
	gboolean ret;

	ret = gdk_color_parse(spec, color);
	color->red ^= xor;
	color->green ^= xor;
	color->blue ^= xor;
	return ret;
}

static void
uber_graph_render_fg_task (UberGraph *graph, /* IN */
                           GraphInfo *info)  /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	RenderClosure closure = { 0 };
	GdkColor fg_color;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	priv = graph->priv;
	/*
	 * Prepare graph closure.
	 */
	closure.graph = graph;
	closure.info = info;
	closure.scale = priv->scale;
	closure.pixel_range.begin = priv->content_rect.y;
	closure.pixel_range.end = priv->content_rect.y + priv->content_rect.height;
	closure.pixel_range.range = priv->content_rect.height;
	closure.value_range = priv->yrange;
	closure.x_epoch = priv->content_rect.x + priv->content_rect.width;
	closure.last_x = -INFINITY;
	closure.last_y = -INFINITY;
	closure.first = TRUE;
	closure.each = ((gdouble)priv->content_rect.width - 2) / ((gdouble)priv->buffer->len - 1.);
	closure.offset = 0;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * Clear the background.
	 */
	cairo_save(info->fg_cairo);
	cairo_set_operator(info->fg_cairo, CAIRO_OPERATOR_CLEAR);
	cairo_rectangle(info->fg_cairo, 0, 0, alloc.width, alloc.height);
	cairo_fill(info->fg_cairo);
	cairo_restore(info->fg_cairo);
	/*
	 * Render data point contents.
	 */
	cairo_save(info->fg_cairo);
	cairo_set_line_cap(info->fg_cairo, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(info->fg_cairo, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(info->fg_cairo,
	              priv->content_rect.x + priv->content_rect.width,
	              priv->content_rect.y + priv->content_rect.height);
	uber_buffer_foreach(priv->scaled, uber_graph_render_fg_each, &closure);
	gdk_color_parse_xor(&fg_color, "#3465a4", 0xFFFF);
	gdk_cairo_set_source_color(info->fg_cairo, &fg_color);
	cairo_stroke(info->fg_cairo);
	cairo_restore(info->fg_cairo);
	priv->fps_off++;
}

#if 0
static void
uber_graph_render_fg_at_offset_task (UberGraph *graph,  /* IN */
                                     GraphInfo *info)   /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkWindow *window;
	//cairo_t *cr;
	gfloat each;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	priv = graph->priv;
	each = (gfloat)priv->content_rect.width / (gfloat)priv->buffer->len / (gfloat)priv->fps;
	window = gtk_widget_get_window(GTK_WIDGET(graph));
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
#if 0
	cr = gdk_cairo_create(GDK_DRAWABLE(window));
	gdk_cairo_set_source_pixmap(cr, info->fg_pixmap,
	                priv->content_rect.x + (each * (gfloat)priv->fps_off),
	                priv->content_rect.y);
	//cairo_set_operator(cr, CAIRO_OPERATOR_XOR);
#endif
	g_debug("Render offset");
	{
		GdkColor black;
		gdk_color_parse_xor(&black, "#000", 0xFFFF);
		gdk_gc_set_foreground(priv->fg_gc, &black);
	}
	gdk_draw_line(GDK_DRAWABLE(window),
	              priv->fg_gc,
	              100, 20, 200, 40);
#if 0
	gdk_draw_drawable(GDK_DRAWABLE(window),
	                  priv->fg_gc,
	                  info->fg_pixmap,
	                  0, 0, 0, 0,
	                  alloc.width, alloc.height);
#endif
#if 0
	cairo_rectangle(cr,
	                priv->content_rect.x,
	                priv->content_rect.y,
	                priv->content_rect.width,
	                priv->content_rect.height);
	cairo_paint(cr);
	cairo_destroy(cr);
#endif
	priv->fps_off++;
}
#endif

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
	info->tick_layout = pango_cairo_create_layout(info->bg_cairo);
	/*
	 * Update the layouts to reflect proper styling.
	 */
	uber_graph_prepare_layout(graph, info->tick_layout, LAYOUT_TICK);
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
	UberRange pixel_range;
	gdouble value;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	BASE_CLASS->size_allocate(widget, alloc);
	priv = UBER_GRAPH(widget)->priv;
	/*
	 * Adjust the sizing of the blit pixmaps.
	 */
	priv->bg_dirty = TRUE;
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[0]);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[1]);
	uber_graph_calculate_rects(UBER_GRAPH(widget));
	uber_graph_set_fps(UBER_GRAPH(widget), priv->fps); /* Re-calculate */
	/*
	 * Rescale values relative to new area.
	 */
	pixel_range.begin = priv->content_rect.y;
	pixel_range.end = priv->content_rect.y + priv->content_rect.height;
	pixel_range.range = priv->content_rect.height;
	for (i = 0; i < priv->scaled->len; i++) {
		if (priv->scaled->buffer[i] != -INFINITY) {
			value = priv->buffer->buffer[i];
			priv->scale(UBER_GRAPH(widget),
			            &priv->yrange,
			            &pixel_range,
			            &value);
			priv->scaled->buffer[i] = value;
		}
	}
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
	dst = expose->window;
	info = &priv->info[priv->flipped];
	/*
	 * Render the background to the pixmap again if needed.
	 */
	if (G_UNLIKELY(priv->bg_dirty)) {
		uber_graph_render_bg_task(UBER_GRAPH(widget), info);
		priv->bg_dirty = FALSE;
	}
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
	 * Blit the foreground if needed.
	 */
	if (G_LIKELY(info->fg_pixmap)) {
		if (G_UNLIKELY(!priv->fps_off)) {
			uber_graph_render_fg_task(UBER_GRAPH(widget), info);
			gdk_draw_drawable(dst, priv->fg_gc, GDK_DRAWABLE(info->fg_pixmap),
							  expose->area.x, expose->area.y,
							  expose->area.x, expose->area.y,
							  expose->area.width, expose->area.height);
		} else {
			GtkAllocation alloc;

			gtk_widget_get_allocation(widget, &alloc);
			gdk_draw_drawable(dst, priv->fg_gc, GDK_DRAWABLE(info->fg_pixmap),
			                  priv->content_rect.x,
			                  priv->content_rect.y,
			                  priv->content_rect.x - (priv->fps_each * priv->fps_off),
			                  priv->content_rect.y,
			                  priv->content_rect.width,
			                  priv->content_rect.height);
			priv->fps_off++;
		}
	}

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
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[0]);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[1]);
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
	gdk_gc_set_function(priv->fg_gc, GDK_XOR);
}

gboolean
uber_scale_linear (UberGraph       *graph,  /* IN */
                   const UberRange *values, /* IN */
                   const UberRange *pixels, /* IN */
                   gdouble         *value)  /* IN/OUT */
{
	#define A (values->range)
	#define B (pixels->range)
	#define C (*value)
	if (*value != 0.) {
		*value = C * B / A;
	}
	return TRUE;
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
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	G_OBJECT_CLASS(uber_graph_parent_class)->finalize(object);
}

static void
uber_graph_get_property (GObject    *object,  /* IN */
                         guint       prop_id, /* IN */
                         GValue     *value,   /* OUT */
                         GParamSpec *pspec)   /* IN */
{
	switch (prop_id) {
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
	switch (prop_id) {
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
	UberGraphPrivate *priv;

	graph->priv = GET_PRIVATE(graph, UBER_TYPE_GRAPH, UberGraphPrivate);
	priv = graph->priv;

	priv->tick_len = 10;
	priv->scale = uber_scale_linear;
	priv->buffer = uber_buffer_new();
	priv->scaled = uber_buffer_new();
	uber_graph_set_fps(graph, 20);
}
