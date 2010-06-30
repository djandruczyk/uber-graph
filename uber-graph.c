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

#ifndef g_malloc0_n
#define g_malloc0_n(a,b) g_malloc0(a * b)
#endif

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
	GdkPixmap   *bg_pixmap;   /* Server-side pixmap for background. */
	GdkPixmap   *fg_pixmap;   /* Server-side pixmap for foreground. */
	cairo_t     *bg_cairo;    /* Cairo context for foreground pixmap. */
	cairo_t     *fg_cairo;    /* Cairo context for background pixmap. */
	PangoLayout *tick_layout; /* TODO */
} GraphInfo;

typedef struct
{
	UberBuffer *buffer;
	UberBuffer *scaled;
	GdkColor    color;
} LineInfo;

struct _UberGraphPrivate
{
	GraphInfo      info[2];      /* Two GraphInfo's for swapping. */
	gboolean       flipped;      /* Which GraphInfo is active. */
	gint           tick_len;     /* Length of axis ticks in pixels. */
	gint           fps;          /* Frames per second. */
	gint           fps_off;      /* Offset in frame-slide */
	gint           fps_to;       /* Frames per second timeout (in MS) */
	gint           stride;       /* Number of data points to store. */
	gfloat         fps_each;     /* How much each frame skews. */
	gfloat         x_each;       /* Precalculated space between points.  */
	guint          fps_handler;  /* GSource identifier for invalidating rect. */
	UberScale      scale;        /* Scaling of values to pixels. */
	UberRange      yrange;       /* Y-Axis range in for raw values. */
	GArray        *lines;        /* Lines to draw. */
	gboolean       bg_dirty;     /* Do we need to update the background. */
	gboolean       fg_dirty;     /* Do we need to update the foreground. */
	gboolean       yautoscale;   /* Should the graph autoscale to handle values
	                              * outside the current range.
	                              */
	GdkGC         *bg_gc;        /* Drawing context for blitting background */
	GdkGC         *fg_gc;        /* Drawing context for blitting foreground */
	GdkRectangle   x_tick_rect;  /* Pre-calculated X tick area. */
	GdkRectangle   y_tick_rect;  /* Pre-calculated Y tick area. */
	GdkRectangle   content_rect; /* Main content area. */
	gchar        **colors;       /* Array of colors to assign. */
	gint           colors_len;   /* Length of colors array. */
	gint           color;        /* Next color to hand out. */
};

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
	gint       offset;
	gboolean   first;
} RenderClosure;

enum
{
	LAYOUT_TICK,
};

static const gchar *default_colors[] = {
	"#3465a4",
	"#73d216",
	"#75507b",
	"#a40000",
};

static void gdk_cairo_rectangle_clean        (cairo_t      *cr,
                                              GdkRectangle *rect);
static void pango_layout_get_pixel_rectangle (PangoLayout  *layout,
                                              GdkRectangle *rect);
static void uber_graph_calculate_rects       (UberGraph    *graph);
static void uber_graph_init_graph_info       (UberGraph    *graph,
                                              GraphInfo    *info);
static void uber_graph_render_fg_shifted_task(UberGraph    *graph,
                                              GraphInfo    *src,
                                              GraphInfo    *dst,
                                              gdouble      *values);

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
	gtk_widget_queue_draw(GTK_WIDGET(graph));
}

/**
 * uber_graph_push:
 * @graph: A UberGraph.
 * @first_id: 
 * @...: 
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_push (UberGraph *graph,    /* IN */
                 gint       first_id, /* IN */
                 ...)                 /* IN */
{
	UberGraphPrivate *priv;
	va_list args;
	gdouble value;
	gdouble *values;
	gint id;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	values = g_malloc0_n(priv->stride, sizeof(gdouble));
	va_start(args, first_id);
	id = first_id;
	while (id != -1) {
		if (id < 0 || id >= priv->stride) {
			g_critical("%s(): %d not a valid line id.", G_STRFUNC, id);
			va_end(args);
			return;
		}
		value = va_arg(args, gdouble);
		values[id] = value;
		id = va_arg(args, gint);
	}
	va_end(args);
	uber_graph_pushv(graph, values);
	g_free(values);
}

/**
 * uber_graph_pushv:
 * @graph: A #UberGraph.
 * @values: An ordered list of line values.
 *
 * Pushes the new values for all of the lines onto the graph.  @values must
 * be an array containing a value for all lines.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_pushv (UberGraph *graph,  /* IN */
                  gdouble   *values) /* IN */
{
	UberGraphPrivate *priv;
	GdkWindow *window;
	UberRange pixel_range;
	gboolean scale_changed = FALSE;
	LineInfo *line;
	gdouble value;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(values != NULL);

	priv = graph->priv;
	priv->fps_off = 0;
	priv->fg_dirty = TRUE;
	pixel_range.begin = priv->content_rect.y;
	pixel_range.end = pixel_range.begin + priv->content_rect.height;
	pixel_range.range = priv->content_rect.height;
	/*
	 * Check if value is outside the current range.
	 */
	if (priv->yautoscale) {
		for (i = 0; i < priv->lines->len; i++) {
			value = values[i];
			if (value > priv->yrange.end) {
				priv->yrange.end = value + ((value - priv->yrange.begin) / 4.);
				priv->yrange.range = priv->yrange.end - priv->yrange.begin;
				priv->bg_dirty = TRUE;
				scale_changed = TRUE;
			} else if (value < priv->yrange.begin) {
				priv->yrange.begin = value - ((priv->yrange.end - value) / 4.);
				priv->yrange.range = priv->yrange.end - priv->yrange.begin;
				priv->bg_dirty = TRUE;
				scale_changed = TRUE;
			}
		}
	}
	for (i = 0; i < priv->lines->len; i++) {
		/*
		 * Push raw data value to buffer.
		 */
		value = values[i];
		line = &g_array_index(priv->lines, LineInfo, i);
		uber_buffer_append(line->buffer, value);
		/*
		 * Scale value and push value to scaled cache.
		 */
		if (!priv->scale(graph, &priv->yrange, &pixel_range, &value)) {
			value = -INFINITY;
		}
		uber_buffer_append(line->scaled, value);
		values[i] = value; /* FIXME: Dont overwrite their memory. */
	}
	/*
	 * Shift the contents of the previous pixmap to its new offset
	 * on the flipped pixmap.  Then draw the new value at the end
	 * of the pixmap and flip said pixmaps.
	 */
	if (G_LIKELY(!scale_changed)) {
		/*
		 * Attempt to render and send as little data as possible to
		 * the X-server since we do not need to render the entire
		 * drawing again.
		 */
		uber_graph_render_fg_shifted_task(graph,
		                                  &priv->info[priv->flipped],
		                                  &priv->info[!priv->flipped],
		                                  values); /* Scaled */
		priv->flipped = !priv->flipped;
		priv->fg_dirty = FALSE;
	}
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
	LineInfo *line;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(stride > 0);

	priv = graph->priv;
	priv->stride = stride;
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		uber_buffer_set_size(line->buffer, stride);
		uber_buffer_set_size(line->scaled, stride);
	}
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

/**
 * uber_graph_set_yautoscale:
 * @graph: A UberGraph.
 * @yautoscale: If y-axis should autoscale to handle values outside of
 *    its current range.
 *
 * Sets the graph to autoscale to handle the current range.  If @yautoscale
 * is %TRUE, new values outside the current y range will cause the range to
 * grow and the graph redrawn to match the new scale.
 *
 * In the future, the graph may compact the scale when the larger values
 * have moved off the graph.  However, this has not yet been implemented.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_yautoscale (UberGraph *graph,      /* IN */
                           gboolean   yautoscale) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->yautoscale = TRUE;
}

/**
 * uber_graph_get_yautoscale:
 * @graph: A UberGraph.
 *
 * Retrieves if the graph is set to autoscale the y-axis.
 *
 * Returns: None.
 * Side effects: None.
 */
gboolean
uber_graph_get_yautoscale (UberGraph *graph) /* IN */
{
	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);
	return graph->priv->yautoscale;
}

/**
 * uber_graph_fps_timeout:
 * @graph: A #UberGraph.
 *
 * GSourceFunc that is called when the amount of time has passed between each
 * frame that needs to be rendered.
 *
 * Returns: %TRUE always.
 * Side effects: None.
 */
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
 * @fps: The number of frames-per-second.
 *
 * Sets the frames-per-second which should be rendered by UberGraph.
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
	priv->fps_to = 1000. / fps;
	priv->fps_each = (gfloat)priv->content_rect.width /
	                 (gfloat)priv->stride /
	                 ((gfloat)priv->fps + 1);
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	priv->fps_handler = g_timeout_add(priv->fps_to,
	                                  uber_graph_fps_timeout,
	                                  graph);
}

/**
 * uber_graph_prepare_layout:
 * @graph: A #UberGraph.
 * @layout: A #PangoLayout.
 * @mode: The layout mode.
 *
 * Prepares the #PangoLayout with the settings required to render the given
 * mode, such as LAYOUT_TICK.
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

	priv = graph->priv;
	if (!(window = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		return;
	}
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
	priv->content_rect.x = priv->y_tick_rect.x + priv->y_tick_rect.width + 1;
	priv->content_rect.y = 1;
	priv->content_rect.width = alloc.width - priv->content_rect.x - 2;
	priv->content_rect.height = priv->x_tick_rect.y - priv->content_rect.y - 2;
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
 * Handles rendering the background to the server-side pixmap.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_bg_task (UberGraph *graph, /* IN */
                           GraphInfo *info)  /* IN */
{
	static const gdouble dashes[] = { 1., 2. };
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
	 * Clear the background to default widget background color.
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
	cairo_move_to(info->bg_cairo, priv->content_rect.x - priv->tick_len, priv->y_tick_rect.y + (priv->y_tick_rect.height / 2) + .5);
	cairo_line_to(info->bg_cairo, priv->content_rect.x + priv->content_rect.width, priv->y_tick_rect.y + (priv->y_tick_rect.height / 2) + .5);
	cairo_stroke(info->bg_cairo);
	//gdk_cairo_rectangle_clean(info->bg_cairo, &priv->y_tick_rect);
	//cairo_set_source_rgb(info->bg_cairo, 0, 0, 0);
	//cairo_fill(info->bg_cairo);
	/*
	 * Cleanup.
	 */
	cairo_restore(info->bg_cairo);
}

/**
 * uber_graph_render_fg_each:
 * @graph: A #UberGraph.
 * @value: The translated value in graph coordinates.
 * @user_data: A RenderClosure.
 *
 * Callback for each data point in the buffer.  Renders the value to the
 * foreground pixmap on the server-side.
 *
 * Returns: %FALSE always.
 * Side effects: None.
 */
static inline gboolean
uber_graph_render_fg_each (UberBuffer *buffer,    /* IN */
                           gdouble     value,     /* IN */
                           gpointer    user_data) /* IN */
{
	UberGraphPrivate *priv;
	RenderClosure *closure = user_data;
	gdouble y;
	gdouble x;

	g_return_val_if_fail(closure->graph != NULL, FALSE);

	priv = closure->graph->priv;
	x = closure->x_epoch - (closure->offset++ * priv->x_each);
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
	               closure->last_x - (priv->x_each / 2.),
	               closure->last_y,
	               closure->last_x - (priv->x_each / 2.),
	               y, x, y);
	goto finish;
  skip:
	cairo_move_to(closure->info->fg_cairo, x,
	              closure->pixel_range.end);
  finish:
  	closure->last_x = x;
  	closure->last_y = y;
	return FALSE;
}

/**
 * gdk_color_parse_xor:
 * @color: A #GdkColor.
 * @spec: A color string ("#fff, #000000").
 * @xor: The value to xor against.
 *
 * Parses a color spec and xor's it against @xor.  The value is stored
 * in @color.
 *
 * Returns: %TRUE if successful; otherwise %FALSE.
 * Side effects: None.
 */
static inline gboolean
gdk_color_parse_xor (GdkColor    *color, /* IN */
                     const gchar *spec,  /* IN */
                     gint         xor)   /* IN */
{
	gboolean ret;

	if ((ret = gdk_color_parse(spec, color))) {
		color->red ^= xor;
		color->green ^= xor;
		color->blue ^= xor;
	}
	return ret;
}

/**
 * uber_graph_stylize_line:
 * @graph: A #UberGraph.
 * @line: A LineInfo.
 * @cr: A cairo context.
 *
 * Stylizes the cairo context according to the lines settings.
 *
 * Returns: None.
 * Side effects: None.
 */
static inline void
uber_graph_stylize_line (UberGraph *graph, /* IN */
                         LineInfo  *line,  /* IN */
                         cairo_t   *cr)    /* IN */
{
	cairo_set_line_width(cr, 2.0);
	gdk_cairo_set_source_color(cr, &line->color);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
}

/**
 * uber_graph_render_fg_task:
 * @graph: A #UberGraph.
 * @info: A GraphInfo.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_fg_task (UberGraph *graph, /* IN */
                           GraphInfo *info)  /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	RenderClosure closure = { 0 };
	LineInfo *line;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
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
	closure.x_epoch = priv->content_rect.x + priv->content_rect.width + priv->x_each;
	/*
	 * Clear the background.
	 */
	cairo_save(info->fg_cairo);
	cairo_set_operator(info->fg_cairo, CAIRO_OPERATOR_CLEAR);
	cairo_rectangle(info->fg_cairo, 0, 0, alloc.width + priv->x_each, alloc.height);
	cairo_fill(info->fg_cairo);
	cairo_restore(info->fg_cairo);
	/*
	 * Render data point contents.
	 */
	cairo_save(info->fg_cairo);
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		closure.last_x = -INFINITY;
		closure.last_y = -INFINITY;
		closure.first = TRUE;
		closure.offset = 0;
		cairo_move_to(info->fg_cairo,
		              closure.x_epoch,
		              priv->content_rect.y + priv->content_rect.height);
		uber_graph_stylize_line(graph, line, info->fg_cairo);
		uber_buffer_foreach(line->scaled, uber_graph_render_fg_each, &closure);
		cairo_stroke(info->fg_cairo);
	}
	cairo_restore(info->fg_cairo);
	priv->fps_off++;
}

/**
 * uber_graph_render_fg_shifted_task:
 * @graph: A #UberGraph.
 *
 * Renders a portion of @src pixmap to @dst pixmap at the shifting
 * rate.  The new value is then rendered onto the new graph and clipped
 * to the proper area.  This prevents the need to send all of the graph
 * contents to the X-server on redraws.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_fg_shifted_task (UberGraph    *graph,  /* IN */
                                   GraphInfo    *src,    /* IN */
                                   GraphInfo    *dst,    /* IN */
                                   gdouble      *values) /* IN */
{
	UberGraphPrivate *priv;
	LineInfo *line;
	GtkAllocation alloc;
	gdouble last_y;
	gdouble x_epoch;
	gdouble y_end;
	gdouble y;
	gint i;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(src != NULL);
	g_return_if_fail(dst != NULL);
	g_return_if_fail(values != NULL);

	priv = graph->priv;
	/*
	 * Clear the old pixmap contents.
	 */
	cairo_save(dst->fg_cairo);
	cairo_set_operator(dst->fg_cairo, CAIRO_OPERATOR_CLEAR);
	cairo_set_source_rgb(dst->fg_cairo, 1, 1, 1);
	cairo_rectangle(dst->fg_cairo, 0, 0, alloc.width, alloc.height);
	cairo_paint(dst->fg_cairo);
	cairo_restore(dst->fg_cairo);
	/*
	 * Shift contents of source onto destination pixmap.  The unused
	 * data point is lost and contents shifted over.
	 */
	gdk_gc_set_function(priv->fg_gc, GDK_COPY);
	gdk_draw_drawable(dst->fg_pixmap, priv->fg_gc, src->fg_pixmap,
	                  priv->content_rect.x + priv->x_each,
	                  priv->content_rect.y,
	                  priv->content_rect.x,
	                  priv->content_rect.y,
	                  priv->content_rect.width,
	                  priv->content_rect.height);
	gdk_gc_set_function(priv->fg_gc, GDK_XOR);
	/*
	 * Render the lines of data.  Clip the region to the new area only.
	 */
	y_end = priv->content_rect.y + priv->content_rect.height;
	x_epoch = priv->content_rect.x + priv->content_rect.width + priv->x_each;
	cairo_save(dst->fg_cairo);
	cairo_rectangle(dst->fg_cairo,
	                priv->content_rect.x + priv->content_rect.width,
	                priv->content_rect.y,
	                priv->x_each,
	                priv->content_rect.height);
	cairo_clip(dst->fg_cairo);
	cairo_set_line_cap(dst->fg_cairo, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(dst->fg_cairo, CAIRO_LINE_JOIN_ROUND);
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		last_y = uber_buffer_get_index(line->scaled, 1);
		/*
		 * Convert relative position to fixed from bottom pixel.
		 */
		y = y_end - values[i];
		last_y = y_end - last_y;
		uber_graph_stylize_line(graph, line, dst->fg_cairo);
		cairo_move_to(dst->fg_cairo, x_epoch, y);
		cairo_curve_to(dst->fg_cairo,
		               x_epoch - (priv->x_each / 2.),
		               y,
		               x_epoch - (priv->x_each / 2.),
		               last_y,
		               priv->content_rect.x + priv->content_rect.width,
		               last_y);
		cairo_stroke(dst->fg_cairo);
	}
	cairo_restore(dst->fg_cairo);
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
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkDrawable *drawable;
	GdkPixmap *bg_pixmap;
	GdkPixmap *fg_pixmap;
	GdkColor bg_color;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_GRAPH(graph));
	g_return_if_fail(info != NULL);

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	drawable = GDK_DRAWABLE(gtk_widget_get_window(GTK_WIDGET(graph)));
	bg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	fg_pixmap = gdk_pixmap_new(drawable, alloc.width + 30, alloc.height, -1);
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
		g_object_unref(info->bg_pixmap);
	}
	if (info->fg_pixmap) {
		g_object_unref(info->fg_pixmap);
	}
	info->bg_pixmap = bg_pixmap;
	info->fg_pixmap = fg_pixmap;
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
	UberGraph *graph;
	UberGraphPrivate *priv;
	UberRange pixel_range;
	LineInfo *line;
	gdouble value;
	gint i;
	gint j;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	BASE_CLASS->size_allocate(widget, alloc);
	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	/*
	 * Adjust the sizing of the blit pixmaps.
	 */
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[0]);
	uber_graph_init_graph_info(UBER_GRAPH(widget), &priv->info[1]);
	uber_graph_calculate_rects(UBER_GRAPH(widget));
	uber_graph_set_fps(graph, priv->fps); /* Re-calculate */
	priv->x_each = ((gdouble)priv->content_rect.width - 2)
	             / ((gdouble)priv->stride - 2.);
	/*
	 * Rescale values relative to new content area.
	 */
	pixel_range.begin = priv->content_rect.y;
	pixel_range.end = priv->content_rect.y + priv->content_rect.height;
	pixel_range.range = priv->content_rect.height;
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		for (j = 0; j < line->buffer->len; j++) {
			if (line->buffer->buffer[j] != -INFINITY) {
				value = line->buffer->buffer[j];
				if (!priv->scale(graph, &priv->yrange, &pixel_range, &value)) {
					value = -INFINITY;
				}
				line->scaled->buffer[j] = value;
			}
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
		/*
		 * FIXME: Use a single background or copy pixmap.
		 */
		uber_graph_render_bg_task(UBER_GRAPH(widget),
		                          &priv->info[!priv->flipped]);
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
		/*
		 * If the foreground is dirty, we need to re-render its entire
		 * contents.
		 */
		if (G_UNLIKELY(priv->fg_dirty)) {
			uber_graph_render_fg_task(UBER_GRAPH(widget), info);
			gdk_draw_drawable(dst, priv->fg_gc, GDK_DRAWABLE(info->fg_pixmap),
							  expose->area.x + 2, expose->area.y,
							  expose->area.x + 2, expose->area.y,
							  expose->area.width - 4, expose->area.height);
			priv->fps_off++;
		} else {
			gdk_draw_drawable(dst, priv->fg_gc, GDK_DRAWABLE(info->fg_pixmap),
			                  priv->content_rect.x,
			                  priv->content_rect.y,
			                  priv->content_rect.x - (priv->fps_each * priv->fps_off),
			                  priv->content_rect.y,
			                  priv->content_rect.width + priv->x_each,
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
 * uber_graph_add_line:
 * @graph: A UberGraph.
 *
 * Adds a new line to the graph.  Values should be added to the graph
 * using uber_graph_push() with the returned line id.
 *
 * Returns: the line-id to use with uber_graph_push().
 * Side effects: None.
 */
guint
uber_graph_add_line (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	LineInfo line = { 0 };

	g_return_val_if_fail(UBER_IS_GRAPH(graph), -1);

	priv = graph->priv;
	line.buffer = uber_buffer_new();
	line.scaled = uber_buffer_new();
	uber_buffer_set_size(line.buffer, priv->stride);
	uber_buffer_set_size(line.scaled, priv->stride);
	gdk_color_parse_xor(&line.color, priv->colors[priv->color], 0xFFFF);
	priv->color = (priv->color + 1) % priv->colors_len;
	g_array_append_val(priv->lines, line);
	return priv->lines->len;
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
	#undef A
	#undef B
	#undef C
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
	LineInfo *line;
	gint i;

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
	for (i = 0; i < priv->lines->len; i++) {
		line = &g_array_index(priv->lines, LineInfo, i);
		uber_buffer_unref(line->buffer);
		uber_buffer_unref(line->scaled);
	}
	g_array_unref(priv->lines);
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
	priv->stride = 60;
	priv->tick_len = 5;
	priv->scale = uber_scale_linear;
	priv->yrange.begin = 0.;
	priv->yrange.end = 1.;
	priv->yrange.range = 1.;
	priv->lines = g_array_sized_new(FALSE, TRUE, sizeof(LineInfo), 2);
	priv->colors = g_strdupv((gchar **)default_colors);
	priv->colors_len = G_N_ELEMENTS(default_colors);
	uber_graph_set_fps(graph, 20);
}
