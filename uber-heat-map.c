/* uber-heat-map.c
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

#include "uber-heat-map.h"

#define WIDGET ((GtkWidgetClass *)uber_heat_map_parent_class)

#define DEBUG_RECT(r)                                       \
    g_debug("GdkRectangle(X=%d, Y=%d, Width=%d, Height=%d", \
            (r).x, (r).y, (r).width, (r).height)
#define GDK_RECTANGLE_RIGHT(r)  ((r).x + (r).width)
#define GDK_RECTANGLE_BOTTOM(r) ((r).y + (r).height)
#define GDK_RECTANGLE_CONTAINS(r, _x, _y)                   \
    ((((_x) > (r).x) && ((_x) < GDK_RECTANGLE_RIGHT(r))) && \
     (((_y) > (r).y) && ((_y) < GDK_RECTANGLE_BOTTOM(r))))

/**
 * SECTION:uber-heat-map.h
 * @title: UberHeatMap
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberHeatMap, uber_heat_map, GTK_TYPE_DRAWING_AREA)

struct _UberHeatMapPrivate
{
	GdkPixmap    *bg_pixmap;
	GdkPixmap    *fg_pixmap;
	GdkPixmap    *hl_pixmap;
	cairo_t      *bg_cairo;
	cairo_t      *fg_cairo;
	cairo_t      *hl_cairo;
	gboolean      bg_dirty;
	gboolean      fg_dirty;
	gboolean      have_rgba;
	gboolean      in_hover;
	gint          x_stride;
	gint          y_stride;
	gint          active_column;
	gint          active_row;
	GdkRectangle  content_rect;
	GdkRectangle  x_tick_rect;
	GdkRectangle  y_tick_rect;
	gint          tick_len;
	UberRange     x_range;
	UberRange     y_range;
	gint          width_block_size;
	gboolean      width_is_count;
	gint          height_block_size;
	gboolean      height_is_count;
	gdouble       cur_block_width;
	gdouble       cur_block_height;
};

/**
 * uber_heat_map_new:
 *
 * Creates a new instance of #UberHeatMap.
 *
 * Returns: the newly created instance of #UberHeatMap.
 * Side effects: None.
 */
GtkWidget*
uber_heat_map_new (void)
{
	return g_object_new(UBER_TYPE_HEAT_MAP, NULL);
}

/**
 * uber_heat_map_clear_cairo:
 * @map: A #UberHeatMap.
 * @cr: A #cairo_t.
 * @width: The width of the context.
 * @height: The height of the context.
 *
 * Clears the contents of a cairo context.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_clear_cairo (UberHeatMap *map,    /* IN */
                           cairo_t     *cr,     /* IN */
                           gint         width,  /* IN */
                           gint         height) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_fill(cr);
	cairo_restore(cr);
}

/**
 * uber_heat_map_init_drawables:
 * @map: A #UberHeatMap.
 *
 * Initializes server-side pixmaps.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_init_drawables (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;
	GdkDrawable *drawable;
	GtkAllocation alloc;
	GdkColormap *colormap;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	drawable = gtk_widget_get_window(GTK_WIDGET(map));
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
	colormap = gdk_screen_get_rgba_colormap(gdk_drawable_get_screen(drawable));
	priv->have_rgba = (colormap != NULL);
	/*
	 * Create server-side pixmaps.
	 */
	priv->bg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	/*
	 * If we have RGBA colormaps, we can draw cleanly with cairo.  Otherwise, we
	 * will need to do more calculation by hand and XOR our content onto the
	 * surface.
	 */
	if (priv->have_rgba) {
		priv->fg_pixmap = gdk_pixmap_new(NULL, alloc.width, alloc.height, 32);
		priv->hl_pixmap = gdk_pixmap_new(NULL, alloc.width, alloc.height, 32);
		gdk_drawable_set_colormap(priv->fg_pixmap, colormap);
		gdk_drawable_set_colormap(priv->hl_pixmap, colormap);
	} else {
		priv->fg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
		priv->hl_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	}
	/*
	 * Setup cairo.
	 */
	priv->bg_cairo = gdk_cairo_create(priv->bg_pixmap);
	priv->fg_cairo = gdk_cairo_create(priv->fg_pixmap);
	priv->hl_cairo = gdk_cairo_create(priv->hl_pixmap);
	uber_heat_map_clear_cairo(map, priv->fg_cairo, alloc.width, alloc.height);
	uber_heat_map_clear_cairo(map, priv->hl_cairo, alloc.width, alloc.height);
}

/**
 * uber_heat_map_destroy_drawables:
 * @map: A #UberHeatMap.
 *
 * Destroys the server-side textures.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_destroy_drawables (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	if (priv->bg_pixmap) {
		g_object_unref(priv->bg_pixmap);
		priv->bg_pixmap = NULL;
	}
	if (priv->fg_pixmap) {
		g_object_unref(priv->fg_pixmap);
		priv->fg_pixmap = NULL;
	}
	if (priv->bg_cairo) {
		cairo_destroy(priv->bg_cairo);
		priv->bg_cairo = NULL;
	}
	if (priv->fg_cairo) {
		cairo_destroy(priv->fg_cairo);
		priv->fg_cairo = NULL;
	}
}

/**
 * uber_heat_map_get_label_size:
 * @map: A #UberHeatMap.
 * @width: A location for the label width.
 * @height: A location for the label height.
 *
 * Calculates the maximum label size required for the graph in pixels.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_get_label_size (UberHeatMap *map,    /* IN */
                              gint        *width,  /* OUT */
                              gint        *height) /* OUT */
{
	UberHeatMapPrivate *priv;
	GdkWindow *window;
	PangoLayout *layout;
	PangoFontDescription *font_desc;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(width != NULL);
	g_return_if_fail(height != NULL);

	priv = map->priv;
	window = gtk_widget_get_window(GTK_WIDGET(map));
	/*
	 * Create cairo/pango resources to calculate size.
	 */
	cr = gdk_cairo_create(GDK_DRAWABLE(window));
	layout = pango_cairo_create_layout(cr);
	font_desc = pango_font_description_new();
	pango_font_description_set_family_static(font_desc, "Monospace");
	pango_font_description_set_size(font_desc, PANGO_SCALE * 8);
	pango_layout_set_font_description(layout, font_desc);
	pango_layout_set_text(layout, "XXXXXXXX", -1);
	pango_layout_get_pixel_size(layout, width, height);
	/*
	 * Cleanup resources.
	 */
	pango_font_description_free(font_desc);
	g_object_unref(layout);
	cairo_destroy(cr);
}

/**
 * uber_heat_map_calculate_rects:
 * @map: A #UberHeatMap.
 *
 * Calculates the various regions of the graph.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_calculate_rects (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;
	GtkAllocation alloc;
	gint label_width;
	gint label_height;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
	/*
	 * Calculate tick label size.
	 */
	uber_heat_map_get_label_size(map, &label_width, &label_height);
	/*
	 * Calculate Y axis tick area.
	 */
	priv->y_tick_rect.y = 1 + (label_height / 2.);
	priv->y_tick_rect.x = 1;
	priv->y_tick_rect.width = label_width
	                        + 3
	                        + priv->tick_len;
	priv->y_tick_rect.height = alloc.height
	                         - priv->y_tick_rect.y
	                         - priv->tick_len
	                         - 3
	                         - label_height;
	/*
	 * Calculate X axis tick area.
	 */
	priv->x_tick_rect.y = priv->y_tick_rect.y + priv->y_tick_rect.height;
	priv->x_tick_rect.x = priv->y_tick_rect.x + priv->y_tick_rect.width;
	priv->x_tick_rect.height = alloc.height - priv->x_tick_rect.y;
	priv->x_tick_rect.width = alloc.width - priv->x_tick_rect.x;
	/*
	 * Calculate main content area.
	 */
	priv->content_rect.x = priv->y_tick_rect.x + priv->y_tick_rect.width;
	priv->content_rect.y = priv->y_tick_rect.y;
	priv->content_rect.width = alloc.width - priv->content_rect.x - 1;
	priv->content_rect.height = priv->x_tick_rect.y - priv->content_rect.y;
}

/**
 * uber_heat_map_render_fg:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_render_fg (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;
	GdkRectangle area;
	gint xcount;
	gint ycount;
	gint ix;
	gint iy;
	gdouble block_width;
	gdouble block_height;
	gdouble alpha;
	GdkColor color;
	GdkColor hl_color;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	gdk_color_parse("#204a87", &color);
	gdk_color_parse("#fce94f", &hl_color);
	cairo_save(priv->fg_cairo);
	cairo_save(priv->hl_cairo);
	/*
	 * Calculate rendering area.
	 */
	area = priv->content_rect;
	area.x += 1;
	area.y += 1;
	area.width -= 2;
	area.height -= 2;
	/*
	 * Calculate the number of x-axis blocks.
	 */
	if (priv->width_is_count) {
		xcount = priv->width_block_size;
	} else {
		xcount = priv->content_rect.width / (gdouble)priv->x_stride;
	}
	block_width = priv->cur_block_width;
	/*
	 * Calculate the number of y-axis blocks.
	 */
	if (priv->height_is_count) {
		ycount = priv->height_block_size;
	} else {
		ycount = priv->content_rect.height / (gdouble)priv->y_stride;
	}
	block_height = priv->cur_block_height;
	/*
	 * Render the contents for the various blocks.
	 */
	for (ix = 0; ix < xcount; ix++) {
		for (iy = 0; iy < ycount; iy++) {
			alpha = g_random_double_range(0., 1.);
			/*
			 * Render the foreground.
			 */
			cairo_rectangle(priv->fg_cairo,
			                area.x + (ix * block_width),
			                area.y + (iy * block_height),
			                block_width,
			                block_height);
			cairo_set_source_rgba(priv->fg_cairo,
			                      color.red / 65535.,
			                      color.green / 65535.,
			                      color.blue / 65535.,
			                      alpha);
			cairo_fill(priv->fg_cairo);
			/*
			 * Render the highlight.
			 */
			cairo_rectangle(priv->hl_cairo,
			                area.x + (ix * block_width),
			                area.y + (iy * block_height),
			                block_width,
			                block_height);
			cairo_set_source_rgba(priv->hl_cairo,
			                      hl_color.red / 65535.,
			                      hl_color.green / 65535.,
			                      hl_color.blue / 65535.,
			                      alpha);
			cairo_fill(priv->hl_cairo);
		}
	}
	/*
	 * Cleanup after resources.
	 */
	cairo_restore(priv->fg_cairo);
	cairo_restore(priv->hl_cairo);
}

/**
 * uber_heat_map_render_x_axis:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_render_x_axis (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
#if 0
	cairo_save(priv->bg_cairo);
	gdk_cairo_rectangle(priv->bg_cairo, &priv->x_tick_rect);
	cairo_set_source_rgba(priv->bg_cairo, 0, 0, 0, .3);
	cairo_fill(priv->bg_cairo);
	cairo_restore(priv->bg_cairo);
#endif
}

/**
 * uber_heat_map_render_y_axis:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_render_y_axis (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
#if 0
	cairo_save(priv->bg_cairo);
	gdk_cairo_rectangle(priv->bg_cairo, &priv->y_tick_rect);
	cairo_set_source_rgba(priv->bg_cairo, 0, 0, 0, .3);
	cairo_fill(priv->bg_cairo);
	cairo_restore(priv->bg_cairo);
#endif
}

/**
 * uber_heat_map_get_active_rect:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_get_active_rect (UberHeatMap  *map,  /* IN */
                               GdkRectangle *rect) /* OUT */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(rect != NULL);

	priv = map->priv;
	rect->x = 1 + priv->content_rect.x + (priv->active_column * priv->cur_block_width);
	rect->y = 1 + priv->content_rect.y + (priv->active_row * priv->cur_block_height);
	rect->width = priv->cur_block_width;
	rect->height = priv->cur_block_height;
}

/**
 * uber_heat_map_render_bg:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_render_bg (UberHeatMap *map) /* IN */
{
	static const gdouble dashes[] = { 1., 2. };
	UberHeatMapPrivate *priv;
	GtkAllocation alloc;
	GtkStyle *style;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	style = gtk_widget_get_style(GTK_WIDGET(map));
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
	cairo_save(priv->bg_cairo);
	/*
	 * Set the background to the default widget bg color.
	 */
	cairo_rectangle(priv->bg_cairo, 0, 0, alloc.width, alloc.height);
	gdk_cairo_set_source_color(priv->bg_cairo, &style->bg[GTK_STATE_NORMAL]);
	cairo_fill(priv->bg_cairo);
	/*
	 * Render the content background.
	 */
	cairo_rectangle(priv->bg_cairo,
	                priv->content_rect.x + .5,
	                priv->content_rect.y + .5,
	                priv->content_rect.width - 1.,
	                priv->content_rect.height - 1.);
	cairo_set_source_rgb(priv->bg_cairo, 1, 1, 1);
	cairo_fill_preserve(priv->bg_cairo);
	/*
	 * Render the content border.
	 */
	cairo_set_source_rgb(priv->bg_cairo, 0, 0, 0);
	cairo_set_dash(priv->bg_cairo, dashes, G_N_ELEMENTS(dashes), .5);
	cairo_set_line_width(priv->bg_cairo, 1.0);
	cairo_stroke(priv->bg_cairo);
	/*
	 * Render the axis labels.
	 */
	uber_heat_map_render_x_axis(map);
	uber_heat_map_render_y_axis(map);
	/*
	 * Cleanup after drawing.
	 */
	cairo_restore(priv->bg_cairo);
}

/**
 * uber_heat_map_expose_event:
 * @widget: A #GtkWidget.
 * @expose: A #GdkEventExpose.
 *
 * Handles the exposure event.
 *
 * Returns: None.
 * Side effects: None.
 */
static gboolean
uber_heat_map_expose_event (GtkWidget      *widget, /* IN */
                            GdkEventExpose *expose) /* IN */
{
	UberHeatMapPrivate *priv;
	GtkAllocation alloc;
	GdkRectangle area;
	cairo_t *cr;

	g_return_val_if_fail(UBER_IS_HEAT_MAP(widget), FALSE);

	priv = UBER_HEAT_MAP(widget)->priv;
	gtk_widget_get_allocation(widget, &alloc);
	/*
	 * Draw the foreground and background if needed.
	 */
	if (priv->bg_dirty) {
		uber_heat_map_render_bg(UBER_HEAT_MAP(widget));
		priv->bg_dirty = FALSE;
	}
	if (priv->fg_dirty) {
		uber_heat_map_render_fg(UBER_HEAT_MAP(widget));
		priv->fg_dirty = FALSE;
	}
	/*
	 * Draw contents to widget surface using cairo.
	 */
	cr = gdk_cairo_create(expose->window);
	/*
	 * Clip to exposure area.
	 */
	gdk_cairo_rectangle(cr, &expose->area);
	cairo_clip(cr);
	/*
	 * Draw the background.
	 */
	gdk_cairo_set_source_pixmap(cr, priv->bg_pixmap, 0, 0);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_paint(cr);
	/*
	 * Draw the foreground.
	 */
	gdk_cairo_set_source_pixmap(cr, priv->fg_pixmap, 0, 0);
	cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
	cairo_paint(cr);
	/*
	 * Draw the highlight rectangle if needed.
	 */
	if (priv->in_hover) {
		if (priv->active_column > -1 && priv->active_row > -1) {
			uber_heat_map_get_active_rect(UBER_HEAT_MAP(widget), &area);
			gdk_cairo_reset_clip(cr, gtk_widget_get_window(widget));
			gdk_cairo_rectangle(cr, &area);
			cairo_clip(cr);
			gdk_cairo_set_source_pixmap(cr, priv->hl_pixmap, 0, 0);
			cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
			cairo_fill(cr);
		}
	}
	/*
	 * Cleanup after drawing.
	 */
	cairo_destroy(cr);
	return FALSE;
}

/**
 * uber_heat_map_set_x_range:
 * @map: A #UberHeatMap.
 * @x_range: An #UberRange.
 *
 * Sets the range of valid inputs for the X axis.  Values outside of this range
 * will not show up on the heat map.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_heat_map_set_x_range (UberHeatMap     *map,     /* IN */
                           const UberRange *x_range) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(x_range != NULL);

	priv = map->priv;
	/*
	 * Store and recalculate range.
	 */
	priv->x_range = *x_range;
	priv->x_range.range = priv->x_range.end - priv->x_range.begin;
	/*
	 * Force full draw of entire widget.
	 */
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(map));
}

/**
 * uber_heat_map_set_y_range:
 * @map: A #UberHeatMap.
 * @x_range: An #UberRange.
 *
 * Sets the range of valid inputs for the X axis.  Values outside of this range
 * will not show up on the heat map.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_heat_map_set_y_range (UberHeatMap     *map,     /* IN */
                           const UberRange *x_range) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(x_range != NULL);

	priv = map->priv;
	/*
	 * Store and recalculate range.
	 */
	priv->x_range = *x_range;
	priv->x_range.range = priv->x_range.end - priv->x_range.begin;
	/*
	 * Force full draw of entire widget.
	 */
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(map));
}

/**
 * uber_heat_map_set_block_size:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_heat_map_set_block_size (UberHeatMap *map,             /* IN */
                              gint         width,           /* IN */
                              gboolean     width_is_count,  /* IN */
                              gint         height,          /* IN */
                              gboolean     height_is_count) /* IN */
{
	UberHeatMapPrivate *priv;
	GtkAllocation alloc;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(width > 0);
	g_return_if_fail(height > 0);

	priv = map->priv;
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
	/*
	 * Store new width/height block size settings.
	 */
	priv->width_block_size = width;
	priv->width_is_count = width_is_count;
	priv->height_block_size = height;
	priv->height_is_count = height_is_count;
	/*
	 * Recalculate the real block sizes.
	 */
	if (width_is_count) {
		priv->cur_block_width = (priv->content_rect.width - 2) / (gdouble)width;
	} else {
		priv->cur_block_width = width;
		priv->x_stride = width;
	}
	if (height_is_count) {
		priv->cur_block_height = (priv->content_rect.height - 2) / (gdouble)height;
	} else {
		priv->cur_block_height = height;
		priv->y_stride = height;
	}
	/*
	 * TODO: Recalculate buckets.
	 */
	/*
	 * Force full draw of entire widget.
	 */
	priv->fg_dirty = TRUE;
	priv->bg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(map));
}

/**
 * uber_heat_map_realize:
 * @map: A #UberHeatMap.
 *
 * Handle the "realize" event.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_realize (GtkWidget *widget) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(widget));

	priv = UBER_HEAT_MAP(widget)->priv;
	WIDGET->realize(widget);
}

/**
 * uber_heat_map_size_allocate:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_size_allocate (GtkWidget     *widget, /* IN */
                             GtkAllocation *alloc)  /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(widget));

	priv = UBER_HEAT_MAP(widget)->priv;
	WIDGET->size_allocate(widget, alloc);
	uber_heat_map_calculate_rects(UBER_HEAT_MAP(widget));
	uber_heat_map_destroy_drawables(UBER_HEAT_MAP(widget));
	uber_heat_map_init_drawables(UBER_HEAT_MAP(widget));
	uber_heat_map_set_block_size(UBER_HEAT_MAP(widget),
	                             priv->width_block_size,
	                             priv->width_is_count,
	                             priv->height_block_size,
	                             priv->height_is_count);
	priv->bg_dirty = TRUE;
	priv->fg_dirty = TRUE;
}

/**
 * uber_heat_map_enter_notify_event:
 * @widget: A #GtkWidget.
 * @crossing: A #GdkEventCrossing.
 *
 * XXX
 *
 * Returns: %FALSE always.
 * Side effects: None.
 */
static gboolean
uber_heat_map_enter_notify_event (GtkWidget        *widget,   /* IN */
                                  GdkEventCrossing *crossing) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_val_if_fail(UBER_IS_HEAT_MAP(widget), FALSE);

	priv = UBER_HEAT_MAP(widget)->priv;
	priv->in_hover = TRUE;
	gdk_window_invalidate_rect(gtk_widget_get_window(widget),
	                           &priv->content_rect,
	                           FALSE);
	return FALSE;
}

/**
 * uber_heat_map_leave_notify_event:
 * @widget: A #GtkWidget.
 * @crossing: A #GdkEventCrossing.
 *
 * XXX
 *
 * Returns: %FALSE always.
 * Side effects: None.
 */
static gboolean
uber_heat_map_leave_notify_event (GtkWidget        *widget,   /* IN */
                                  GdkEventCrossing *crossing) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_val_if_fail(UBER_IS_HEAT_MAP(widget), FALSE);

	priv = UBER_HEAT_MAP(widget)->priv;
	priv->in_hover = FALSE;
	gdk_window_invalidate_rect(gtk_widget_get_window(widget),
	                           &priv->content_rect,
	                           FALSE);
	return FALSE;
}

/**
 * uber_heat_map_motion_notify_event:
 * @widget: A #GtkWidget.
 * @motion: A #GdkEventMotion.
 *
 * Handles the motion event within the widget.  This is used to determine
 * the currently hovered block that is active.
 *
 * Returns: %FALSE always.
 * Side effects: None.
 */
static gboolean
uber_heat_map_motion_notify_event (GtkWidget      *widget, /* IN */
                                   GdkEventMotion *motion) /* IN */
{
	UberHeatMapPrivate *priv;
	gdouble x_offset;
	gdouble y_offset;
	gint active_column = -1;
	gint active_row = -1;
	gchar *tooltip;

	g_return_val_if_fail(UBER_IS_HEAT_MAP(widget), FALSE);

	priv = UBER_HEAT_MAP(widget)->priv;
	if (GDK_RECTANGLE_CONTAINS(priv->content_rect, motion->x, motion->y)) {
		/*
		 * Get relative coordinate within the content area.
		 */
		x_offset = motion->x - priv->content_rect.x;
		y_offset = motion->y - priv->content_rect.y;
		/*
		 * Set the active row and column.
		 */
		active_column = x_offset / priv->cur_block_width;
		active_row = y_offset / priv->cur_block_height;
	}
	if ((active_column != priv->active_column) ||
	    (active_row != priv->active_row)) {
	    if (active_column > -1 && active_row > -1) {
			/*
			 * TODO: Add format callback to determine tooltip contents.
			 */
			tooltip = g_strdup_printf("Row %d\nColumn %d", active_row, active_column);
			gtk_widget_set_tooltip_text(widget, tooltip);
			g_free(tooltip);
		} else {
			gtk_widget_set_tooltip_text(widget, "");
		}
		gdk_window_invalidate_rect(gtk_widget_get_window(widget),
		                           &priv->content_rect, FALSE);
	}
	priv->active_column = active_column;
	priv->active_row = active_row;
	return FALSE;
}

/**
 * uber_heat_map_size_request:
 * @widget: A #GtkWidget.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_size_request (GtkWidget      *widget, /* IN */
                            GtkRequisition *req)    /* OUT */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(widget));

	priv = UBER_HEAT_MAP(widget)->priv;
	req->width = priv->y_tick_rect.width + priv->x_stride;
	req->height = priv->x_tick_rect.height + priv->y_stride;
}

/**
 * uber_heat_map_finalize:
 * @object: A #UberHeatMap.
 *
 * Finalizer for a #UberHeatMap instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_finalize (GObject *object) /* IN */
{
	uber_heat_map_destroy_drawables(UBER_HEAT_MAP(object));
	G_OBJECT_CLASS(uber_heat_map_parent_class)->finalize(object);
}

/**
 * uber_heat_map_class_init:
 * @klass: A #UberHeatMapClass.
 *
 * Initializes the #UberHeatMapClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_class_init (UberHeatMapClass *klass) /* IN */
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_heat_map_finalize;
	g_type_class_add_private(object_class, sizeof(UberHeatMapPrivate));

	widget_class = GTK_WIDGET_CLASS(klass);
	widget_class->realize = uber_heat_map_realize;
	widget_class->expose_event = uber_heat_map_expose_event;
	widget_class->size_allocate = uber_heat_map_size_allocate;
	widget_class->enter_notify_event = uber_heat_map_enter_notify_event;
	widget_class->leave_notify_event = uber_heat_map_leave_notify_event;
	widget_class->motion_notify_event = uber_heat_map_motion_notify_event;
	widget_class->size_request = uber_heat_map_size_request;
}

/**
 * uber_heat_map_init:
 * @map: A #UberHeatMap.
 *
 * Initializes the newly created #UberHeatMap instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_init (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;
	GdkEventMask mask = 0;

	map->priv = G_TYPE_INSTANCE_GET_PRIVATE(map,
	                                        UBER_TYPE_HEAT_MAP,
	                                        UberHeatMapPrivate);
	priv = map->priv;

	/*
	 * Setup defaults.
	 */
	priv->tick_len = 10;
	priv->active_column = -1;
	priv->active_row = -1;
	uber_heat_map_set_block_size(map, 20, TRUE, 10, TRUE);

	/*
	 * Enable required GdkEvents.
	 */
	mask |= GDK_ENTER_NOTIFY_MASK;
	mask |= GDK_LEAVE_NOTIFY_MASK;
	mask |= GDK_POINTER_MOTION_MASK;
	gtk_widget_set_events(GTK_WIDGET(map), mask);
}
