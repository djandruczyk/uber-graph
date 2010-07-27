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

#define WIDGET_CLASS (GTK_WIDGET_CLASS(uber_graph_parent_class))

/**
 * SECTION:uber-graph.h
 * @title: UberGraph
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_ABSTRACT_TYPE(UberGraph, uber_graph, GTK_TYPE_DRAWING_AREA)

typedef struct
{
	GdkPixmap *bg_pixmap;  /* Server side pixmap for background. */
	GdkPixmap *fg_pixmap;  /* Server side pixmap for foreground. */
	cairo_t   *bg_cairo;   /* Cairo context for background. */
	cairo_t   *fg_cairo;   /* Cairo context for foreground. */
} GraphTexture;

struct _UberGraphPrivate
{
	GraphTexture texture[2];    /* Front and back textures. */
	gboolean     flipped;       /* Which texture are we using. */
	GdkRectangle content_rect;  /* Content area rectangle. */
	GdkRectangle nonvis_rect;   /* Non-visible drawing area larger than
	                             * content rect. Used to draw over larger
	                             * area so we can scroll and not fall out
	                             * of view.
	                             */
	gboolean     have_rgba;     /* Do we support 32-bit RGBA colormaps. */
	gint         x_slots;       /* Number of data points on x axis. */
	gint         fps;           /* Desired frames per second. */
	gint         fps_real;      /* Milleseconds between FPS callbacks. */
	gint         fps_off;       /* Frame offset since last data point. */
	gfloat       fps_each;      /* How far to move in each FPS tick. */
	guint        fps_handler;   /* Timeout for moving the content. */
	gfloat       dps;           /* Desired data points per second. */
	gfloat       dps_each;      /* How many pixels between data points. */
	GTimeVal     dps_tv;        /* Timeval of last data point. */
	guint        dps_handler;   /* Timeout for getting new data. */
	gboolean     fg_dirty;      /* Does the foreground need to be redrawn. */
	gboolean     bg_dirty;      /* Does the background need to be redrawn. */
	guint        tick_len;      /* How long should axis-ticks be. */
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
 * uber_graph_fps_timeout:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: %TRUE always.
 * Side effects: None.
 */
static gboolean
uber_graph_fps_timeout (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	if (priv->fps_off) {
		gtk_widget_queue_draw_area(GTK_WIDGET(graph),
		                           priv->content_rect.x,
		                           priv->content_rect.y,
		                           priv->content_rect.width,
		                           priv->content_rect.height);
	}
	priv->fps_off++;
	return TRUE;
}

/**
 * uber_graph_get_next_data:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static inline gboolean
uber_graph_get_next_data (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	g_debug("%s()", G_STRFUNC);
	/*
	 * Get the current time for this data point.  This is used to calculate
	 * the proper offset in the FPS callback.
	 */
	priv = graph->priv;
	g_get_current_time(&priv->dps_tv);
	/*
	 * Notify the subclass to retrieve the data point.
	 */
	if (UBER_GRAPH_GET_CLASS(graph)->get_next_data) {
		return UBER_GRAPH_GET_CLASS(graph)->get_next_data(graph);
	}
	return FALSE;
}

/**
 * uber_graph_init_texture:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_init_texture (UberGraph    *graph,   /* IN */
                         GraphTexture *texture) /* OUT */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	GdkDrawable *drawable;
	GdkColormap *colormap;
	GdkVisual *visual;
	gint depth = 32;
	gint width;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * Get drawable to base pixmaps upon.
	 */
	if (!(drawable = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		g_critical("%s() called before GdkWindow is allocated.", G_STRFUNC);
		return;
	}
	/*
	 * If we do not have 32-bit RGBA colormaps, we need to fallback to XOR
	 * rendering.
	 */
	if (!priv->have_rgba) {
		drawable = NULL;
		depth = -1;
	}
	/*
	 * Initialize foreground and background pixmaps.
	 */
	width = MAX(priv->nonvis_rect.x + priv->nonvis_rect.width, alloc.width);
	texture->fg_pixmap = gdk_pixmap_new(drawable, width, alloc.height, depth);
	texture->bg_pixmap = gdk_pixmap_new(drawable, width, alloc.height, depth);
	/*
	 * Create a 32-bit colormap if needed.
	 */
	if (priv->have_rgba) {
		visual = gdk_visual_get_best_with_depth(depth);
		colormap = gdk_colormap_new(visual, FALSE);
		gdk_drawable_set_colormap(GDK_DRAWABLE(texture->fg_pixmap), colormap);
		gdk_drawable_set_colormap(GDK_DRAWABLE(texture->bg_pixmap), colormap);
		g_object_unref(colormap);
	}
	/*
	 * Create cairo textures for drawing.
	 */
	texture->fg_cairo = gdk_cairo_create(texture->fg_pixmap);
	texture->bg_cairo = gdk_cairo_create(texture->bg_pixmap);
}

/**
 * uber_graph_destroy_texture:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_destroy_texture (UberGraph    *graph,   /* IN */
                            GraphTexture *texture) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	if (texture->fg_cairo) {
		cairo_destroy(texture->fg_cairo);
		texture->fg_cairo = NULL;
	}
	if (texture->bg_cairo) {
		cairo_destroy(texture->bg_cairo);
		texture->bg_cairo = NULL;
	}
	if (texture->fg_pixmap) {
		g_object_unref(texture->fg_pixmap);
		texture->fg_pixmap = NULL;
	}
	if (texture->bg_pixmap) {
		g_object_unref(texture->bg_pixmap);
		texture->bg_pixmap = NULL;
	}
}

/**
 * uber_graph_calculate_rects:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_calculate_rects (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;
	PangoLayout *layout;
	PangoFontDescription *font_desc;
	GdkDrawable *drawable;
	gint pango_width;
	gint pango_height;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	/*
	 * We can't calculate rectangles before we have a GdkWindow.
	 */
	if (!(drawable = gtk_widget_get_window(GTK_WIDGET(graph)))) {
		return;
	}
	/*
	 * Determine the pixels required for labels.
	 */
	cr = gdk_cairo_create(drawable);
	layout = pango_cairo_create_layout(cr);
	font_desc = pango_font_description_new();
	pango_font_description_set_family_static(font_desc, "Monospace");
	pango_font_description_set_size(font_desc, 8 * PANGO_SCALE);
	pango_layout_set_font_description(layout, font_desc);
	pango_layout_set_text(layout, "XXXXXXXX", -1);
	pango_layout_get_pixel_size(layout, &pango_width, &pango_height);
	pango_font_description_free(font_desc);
	g_object_unref(layout);
	cairo_destroy(cr);
	/*
	 * Calculate content area rectangle.
	 */
	priv->content_rect.x = priv->tick_len + pango_width + 1.5;
	priv->content_rect.y = 1.5;
	priv->content_rect.width = alloc.width - priv->content_rect.x - 3.0;
	priv->content_rect.height = alloc.height - priv->tick_len - pango_height - 3.0;
	/*
	 * Calculate FPS/DPS adjustments.
	 */
	priv->dps_each = (gfloat)priv->content_rect.width / (gfloat)(priv->x_slots - 1);
	priv->fps_each = priv->dps_each / (gfloat)priv->fps;
	if (priv->fps_each < 1.) {
		priv->fps_each = 1;
		priv->fps_real = 1000. / priv->dps_each;
	} else {
		priv->fps_real = 1000. / priv->fps;
	}
	/*
	 * Update FPS callback.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
		priv->fps_handler = g_timeout_add(priv->fps_real,
		                                  (GSourceFunc)uber_graph_fps_timeout,
		                                  graph);
	}
	/*
	 * Calculate the non-visible area that drawing should happen within.
	 */
	priv->nonvis_rect = priv->content_rect;
	priv->nonvis_rect.width += priv->dps_each + 2;
}

/**
 * uber_graph_dps_timeout:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: %TRUE always.
 * Side effects: None.
 */
static gboolean
uber_graph_dps_timeout (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), FALSE);

	priv = graph->priv;
	if (!uber_graph_get_next_data(graph)) {
		/*
		 * XXX: How should we handle failed data retrieval.
		 */
	}
	/*
	 * Make sure the content is re-rendered.
	 */
	priv->fg_dirty = TRUE;
	priv->fps_off = 0;
	gtk_widget_queue_draw_area(GTK_WIDGET(graph),
	                           priv->content_rect.x,
	                           priv->content_rect.y,
	                           priv->content_rect.width,
	                           priv->content_rect.height);
	return TRUE;
}

/**
 * uber_graph_register_dps_handler:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_register_dps_handler (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	guint dps_freq;
	gboolean do_now = TRUE;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	if (priv->dps_handler) {
		g_source_remove(priv->dps_handler);
		do_now = FALSE;
	}
	/*
	 * Calculate the update frequency.
	 */
	dps_freq = 1000 / priv->dps;
	/*
	 * Install the data handler.
	 */
	priv->dps_handler = g_timeout_add(dps_freq,
	                                  (GSourceFunc)uber_graph_dps_timeout,
	                                  graph);
	/*
	 * Call immediately.
	 */
	if (do_now) {
		uber_graph_dps_timeout(graph);
	}
}

/**
 * uber_graph_set_dps:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_dps (UberGraph *graph, /* IN */
                    gfloat     dps)   /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->dps = dps;
	/*
	 * TODO: Does this belong somewhere else?
	 */
	if (UBER_GRAPH_GET_CLASS(graph)->set_stride) {
		UBER_GRAPH_GET_CLASS(graph)->set_stride(graph, priv->x_slots);
	}
	uber_graph_register_dps_handler(graph);
}

/**
 * uber_graph_register_fps_handler:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_register_fps_handler (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Remove any existing FPS handler.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	/*
	 * Install the FPS timeout.
	 */
	priv->fps_handler = g_timeout_add(priv->fps_real,
	                                  (GSourceFunc)uber_graph_fps_timeout,
	                                  graph);
}

/**
 * uber_graph_set_fps:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_graph_set_fps (UberGraph *graph, /* IN */
                    guint      fps)   /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	priv->fps = fps;
	uber_graph_register_fps_handler(graph);
}

/**
 * uber_graph_realize:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_realize (GtkWidget *widget) /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	WIDGET_CLASS->realize(widget);
	/*
	 * Calculate new layout based on allocation.
	 */
	uber_graph_calculate_rects(graph);
	/*
	 * Re-initialize textures for updated sizes.
	 */
	uber_graph_destroy_texture(graph, &priv->texture[0]);
	uber_graph_destroy_texture(graph, &priv->texture[1]);
	uber_graph_init_texture(graph, &priv->texture[0]);
	uber_graph_init_texture(graph, &priv->texture[1]);
	/*
	 * Notify subclass of current data stride (points per graph).
	 */
	if (UBER_GRAPH_GET_CLASS(widget)->set_stride) {
		UBER_GRAPH_GET_CLASS(widget)->set_stride(UBER_GRAPH(widget),
		                                         priv->x_slots);
	}
	/*
	 * Install the data collector.
	 */
	uber_graph_register_dps_handler(graph);
}

/**
 * uber_graph_unrealize:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_unrealize (GtkWidget *widget) /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	/*
	 * Unregister any data acquisition handlers.
	 */
	if (priv->dps_handler) {
		g_source_remove(priv->dps_handler);
		priv->dps_handler = 0;
	}
	/*
	 * Destroy textures.
	 */
	uber_graph_destroy_texture(graph, &priv->texture[0]);
	uber_graph_destroy_texture(graph, &priv->texture[1]);
}

/**
 * uber_graph_screen_changed:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_screen_changed (GtkWidget *widget,     /* IN */
                           GdkScreen *old_screen) /* IN */
{
	UberGraphPrivate *priv;
	GdkScreen *screen;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	/*
	 * Check if we have RGBA colormaps available.
	 */
	priv->have_rgba = FALSE;
	if ((screen = gtk_widget_get_screen(widget))) {
		priv->have_rgba = !!gdk_screen_get_rgba_colormap(screen);
	}
}

/**
 * uber_graph_show:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_show (GtkWidget *widget) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	WIDGET_CLASS->show(widget);
	/*
	 * Only run the FPS timeout when we are visible.
	 */
	uber_graph_register_fps_handler(UBER_GRAPH(widget));
}

/**
 * uber_graph_hide:
 * @widget: A #GtkWIdget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_hide (GtkWidget *widget) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	/*
	 * Disable the FPS timeout when we are not visible.
	 */
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
		priv->fps_handler = 0;
	}
}

static inline void
uber_graph_get_pixmap_rect (UberGraph    *graph, /* IN */
                            GdkRectangle *rect)  /* OUT */
{
	UberGraphPrivate *priv;
	GtkAllocation alloc;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	rect->x = 0;
	rect->y = 0;
	rect->width = MAX(alloc.width,
	                  priv->nonvis_rect.x + priv->nonvis_rect.width);
	rect->height = alloc.height;
}

/**
 * uber_graph_render_fg:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_fg (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GraphTexture *texture;
	GdkRectangle rect;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	g_debug("%s()", G_STRFUNC);
	priv = graph->priv;
	uber_graph_get_pixmap_rect(graph, &rect);
	texture = &priv->texture[priv->flipped];
	/*
	 * Render to texture if needed.
	 */
	if (priv->fg_dirty) {
		/*
		 * Clear content area.
		 */
		cairo_save(texture->fg_cairo);
		cairo_set_operator(texture->fg_cairo, CAIRO_OPERATOR_CLEAR);
		gdk_cairo_rectangle(texture->fg_cairo, &rect);
		cairo_fill(texture->fg_cairo);
		cairo_restore(texture->fg_cairo);
		/*
		 * Render the new content.
		 */
		if (UBER_GRAPH_GET_CLASS(graph)->render) {
			cairo_save(texture->fg_cairo);
			gdk_cairo_rectangle(texture->fg_cairo, &priv->nonvis_rect);
			cairo_clip(texture->fg_cairo);
			UBER_GRAPH_GET_CLASS(graph)->render(graph,
			                                    texture->fg_cairo,
			                                    &priv->nonvis_rect);
			cairo_restore(texture->fg_cairo);
		}
	}
	/*
	 * Foreground is no longer dirty.
	 */
	priv->fg_dirty = FALSE;
}

/**
 * uber_graph_render_bg:
 * @graph: A #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_render_bg (UberGraph *graph) /* IN */
{
	const gdouble dashes[] = { 1.0, 2.0 };
	UberGraphPrivate *priv;
	GraphTexture *texture;
	GtkAllocation alloc;
	GtkStyle *style;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	g_debug("%s()", G_STRFUNC);
	priv = graph->priv;
	gtk_widget_get_allocation(GTK_WIDGET(graph), &alloc);
	style = gtk_widget_get_style(GTK_WIDGET(graph));
	texture = &priv->texture[priv->flipped];
	/*
	 * Paint the background.
	 */
	cairo_save(texture->bg_cairo);
	gdk_cairo_set_source_color(texture->bg_cairo, &style->bg[GTK_STATE_NORMAL]);
	cairo_rectangle(texture->bg_cairo, 0, 0, alloc.width, alloc.height);
	cairo_fill(texture->bg_cairo);
	cairo_restore(texture->bg_cairo);
	/*
	 * Paint the content area background.
	 */
	cairo_save(texture->bg_cairo);
	gdk_cairo_set_source_color(texture->bg_cairo, &style->light[GTK_STATE_NORMAL]);
	gdk_cairo_rectangle(texture->bg_cairo, &priv->content_rect);
	cairo_fill(texture->bg_cairo);
	cairo_restore(texture->bg_cairo);
	/*
	 * Stroke the border around the content area.
	 */
	cairo_save(texture->bg_cairo);
	gdk_cairo_set_source_color(texture->bg_cairo, &style->fg[GTK_STATE_NORMAL]);
	cairo_set_line_width(texture->bg_cairo, 1.0);
	cairo_set_dash(texture->bg_cairo, dashes, G_N_ELEMENTS(dashes), 0);
	cairo_set_antialias(texture->bg_cairo, CAIRO_ANTIALIAS_NONE);
	cairo_rectangle(texture->bg_cairo,
	                priv->content_rect.x - .5,
	                priv->content_rect.y - .5,
	                priv->content_rect.width + 1.0,
	                priv->content_rect.height + 1.0);
	cairo_stroke(texture->bg_cairo);
	cairo_restore(texture->bg_cairo);
	/*
	 * Background is no longer dirty.
	 */
	priv->bg_dirty = FALSE;
}

static inline void
time_val_diff_msec (GTimeVal *a, /* IN */
                    GTimeVal *b, /* IN */
                    GTimeVal *c) /* OUT */
{
	g_return_if_fail(a != NULL);
	g_return_if_fail(b != NULL);
	g_return_if_fail(c != NULL);

	c->tv_sec = b->tv_sec - a->tv_sec;
	c->tv_usec = b->tv_usec - a->tv_usec;
	if (c->tv_usec < 0) {
		c->tv_usec += G_USEC_PER_SEC;
		c->tv_sec -= 1;
	}
}

/**
 * uber_graph_get_fps_offset:
 * @graph: A #UberGraph.
 *
 * Calculates the number of pixels that the foreground should be rendered
 * from the origin.
 *
 * Returns: The pixel offset to render the foreground.
 * Side effects: None.
 */
static gfloat
uber_graph_get_fps_offset (UberGraph *graph) /* IN */
{
	UberGraphPrivate *priv;
	GTimeVal rel;
	GTimeVal tv;
	gfloat f;

	g_return_val_if_fail(UBER_IS_GRAPH(graph), 0.);

	priv = graph->priv;
	/*
	 * Get the time difference between now and last sample in milliseconds.
	 */
	g_get_current_time(&tv);
	time_val_diff_msec(&priv->dps_tv, &tv, &rel);
	f = (rel.tv_sec * 1000) + (rel.tv_usec / 1000.);
	/*
	 * Determine the number of frames we should be off from the origin
	 * by dividing the msec offset by the msec between callbacks.
	 */
	f /= (gfloat)priv->fps_real;
	/*
	 * f is now the number of frames we should be from the origin.
	 */
	return MIN(f * priv->fps_each, priv->dps_each);
}

/**
 * uber_graph_expose_event:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: %FALSE always.
 * Side effects: None.
 */
static gboolean
uber_graph_expose_event (GtkWidget      *widget, /* IN */
                         GdkEventExpose *expose) /* IN */
{
	UberGraphPrivate *priv;
	GraphTexture *texture;
	GtkAllocation alloc;
	cairo_t *cr;
	gfloat offset;

	g_return_val_if_fail(UBER_IS_GRAPH(widget), FALSE);

	priv = UBER_GRAPH(widget)->priv;
	gtk_widget_get_allocation(widget, &alloc);
	texture = &priv->texture[priv->flipped];
	/*
	 * Ensure that the texture is initialized.
	 */
	g_assert(texture->fg_pixmap);
	g_assert(texture->bg_pixmap);
	/*
	 * Allocate resources.
	 */
	cr = gdk_cairo_create(expose->window);
	/*
	 * Clip to exposure area.
	 */
	gdk_cairo_rectangle(cr, &expose->area);
	cairo_clip(cr);
	/*
	 * Render background or foreground if needed.
	 */
	if (priv->bg_dirty) {
		uber_graph_render_bg(UBER_GRAPH(widget));
	}
	if (priv->fg_dirty) {
		uber_graph_render_fg(UBER_GRAPH(widget));
	}
	/*
	 * Paint the background to the exposure area.
	 */
	cairo_save(cr);
	gdk_cairo_set_source_pixmap(cr, texture->bg_pixmap, 0, 0);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_paint(cr);
	cairo_restore(cr);
	/*
	 * Draw the foreground.
	 */
	offset = uber_graph_get_fps_offset(UBER_GRAPH(widget));
	if (priv->have_rgba) {
		cairo_save(cr);
		/*
		 * Clip exposure to the content area.
		 */
		gdk_cairo_reset_clip(cr, expose->window);
		gdk_cairo_rectangle(cr, &priv->content_rect);
		cairo_clip(cr);
		/*
		 * Draw the foreground to the widget.
		 */
		gdk_cairo_set_source_pixmap(cr, texture->fg_pixmap, -offset, 0);
		cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		/*
		 * TODO: Use XOR command for fallback.
		 */
		g_warn_if_reached();
	}
	/*
	 * Cleanup resources.
	 */
	cairo_destroy(cr);
	return FALSE;
}

/**
 * uber_graph_style_set:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_style_set (GtkWidget *widget,    /* IN */
                      GtkStyle  *old_style) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(widget));

	priv = UBER_GRAPH(widget)->priv;
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(widget);
}

/**
 * uber_graph_size_allocate:
 * @widget: A #GtkWidget.
 *
 * XXX
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

	g_return_if_fail(UBER_IS_GRAPH(widget));

	graph = UBER_GRAPH(widget);
	priv = graph->priv;
	WIDGET_CLASS->size_allocate(widget, alloc);
	/*
	 * If there is no window yet, we can defer setup.
	 */
	if (!gtk_widget_get_window(widget)) {
		return;
	}
	/*
	 * Recalculate rectangles.
	 */
	uber_graph_calculate_rects(graph);
	/*
	 * Recreate server side pixmaps.
	 */
	uber_graph_destroy_texture(graph, &priv->texture[0]);
	uber_graph_destroy_texture(graph, &priv->texture[1]);
	uber_graph_init_texture(graph, &priv->texture[0]);
	uber_graph_init_texture(graph, &priv->texture[1]);
	/*
	 * Mark foreground and background as dirty.
	 */
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(widget);
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
	G_OBJECT_CLASS(uber_graph_parent_class)->finalize(object);
}

/**
 * uber_graph_dispose:
 * @object: A #GObject.
 *
 * Dispose callback for @object.  This method releases references held
 * by the #GObject instance.
 *
 * Returns: None.
 * Side effects: Plenty.
 */
static void
uber_graph_dispose (GObject *object) /* IN */
{
	UberGraph *graph;
	UberGraphPrivate *priv;

	graph = UBER_GRAPH(object);
	priv = graph->priv;
	uber_graph_destroy_texture(graph, &priv->texture[0]);
	uber_graph_destroy_texture(graph, &priv->texture[1]);
	G_OBJECT_CLASS(uber_graph_parent_class)->dispose(object);
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
	object_class->dispose = uber_graph_dispose;
	g_type_class_add_private(object_class, sizeof(UberGraphPrivate));

	widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->expose_event = uber_graph_expose_event;
	widget_class->hide = uber_graph_hide;
	widget_class->realize = uber_graph_realize;
	widget_class->screen_changed = uber_graph_screen_changed;
	widget_class->show = uber_graph_show;
	widget_class->size_allocate = uber_graph_size_allocate;
	widget_class->style_set = uber_graph_style_set;
	widget_class->unrealize = uber_graph_unrealize;
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

	/*
	 * Store pointer to private data allocation.
	 */
	graph->priv = G_TYPE_INSTANCE_GET_PRIVATE(graph,
	                                          UBER_TYPE_GRAPH,
	                                          UberGraphPrivate);
	priv = graph->priv;
	/*
	 * Prepare default values.
	 */
	priv->tick_len = 10;
	priv->fps = 20;
	priv->fps_real = 1000. / priv->fps;
	priv->dps = 1.;
	priv->x_slots = 60;
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
}
