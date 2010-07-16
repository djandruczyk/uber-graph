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

#include <math.h>

#include "g-ring.h"
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
	GdkPixmap       *bg_pixmap;
	GdkPixmap       *fg_pixmap;
	GdkPixmap       *hl_pixmap;
	cairo_t         *bg_cairo;
	cairo_t         *fg_cairo;
	cairo_t         *hl_cairo;
	gboolean         bg_dirty;
	gboolean         fg_dirty;
	gboolean         full_draw;
	gboolean         have_rgba;
	gboolean         in_hover;
	gint             fps;
	gint             fps_calc;
	gdouble          fps_each;
	gint             fps_to;
	guint            fps_handler;
	gint             fps_off;
	gint             stride;
	gint             col_count;
	gint             row_count;
	gint             active_column;
	gint             active_row;
	GdkRectangle     content_rect;
	GdkRectangle     x_tick_rect;
	GdkRectangle     y_tick_rect;
	gint             tick_len;

	UberRange        x_range;
	UberRange        y_range;

	gint             width_block_size;
	gboolean         width_is_count;
	gint             height_block_size;
	gboolean         height_is_count;
	gdouble          cur_block_width;
	gdouble          cur_block_height;

	UberHeatMapFunc  value_func;
	gpointer         value_user_data;
	GDestroyNotify   value_notify;

	GRing           *ring;
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
 * uber_heat_map_get_next_values:
 * @map: A #UberHeatMap.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static gboolean
uber_heat_map_get_next_values (UberHeatMap  *map,    /* IN */
                               GArray      **values) /* OUT */
{
	UberHeatMapPrivate *priv;

	g_return_val_if_fail(UBER_IS_HEAT_MAP(map), FALSE);
	g_return_val_if_fail(values != NULL, FALSE);

	priv = map->priv;
	if (!priv->value_func) {
		return FALSE;
	}
	return priv->value_func(map, values, priv->value_user_data);
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
	if (priv->hl_pixmap) {
		g_object_unref(priv->hl_pixmap);
		priv->hl_pixmap = NULL;
	}
	if (priv->bg_cairo) {
		cairo_destroy(priv->bg_cairo);
		priv->bg_cairo = NULL;
	}
	if (priv->fg_cairo) {
		cairo_destroy(priv->fg_cairo);
		priv->fg_cairo = NULL;
	}
	if (priv->hl_cairo) {
		cairo_destroy(priv->hl_cairo);
		priv->hl_cairo = NULL;
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
 * @full_draw: Redraw the entire contents.
 *
 * Renders the foreground.  If @full_draw is %TRUE, then we will draw the
 * entire contents rather than shift the current data and draw just the new
 * content.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_render_fg (UberHeatMap *map,       /* IN */
                         gboolean     full_draw) /* IN */
{
	UberHeatMapPrivate *priv;
	GtkAllocation alloc;
	GdkRectangle area;
	gint xcount;
	gint ycount;
	gint ix;
	gint iy;
	gdouble alpha;
	gdouble block_width;
	gdouble block_height;
	GdkColor color;
	GdkColor hl_color;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
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
	 * Set clipping regions.
	 */
	gdk_cairo_rectangle(priv->fg_cairo, &area);
	gdk_cairo_rectangle(priv->hl_cairo, &area);
	cairo_clip(priv->fg_cairo);
	cairo_clip(priv->hl_cairo);
	/*
	 * Calculate the number of x-axis blocks.
	 */
	if (priv->width_is_count) {
		xcount = priv->width_block_size;
	} else {
		xcount = priv->content_rect.width / (gdouble)priv->col_count;
	}
	block_width = priv->cur_block_width;
	/*
	 * Calculate the number of y-axis blocks.
	 */
	if (priv->height_is_count) {
		ycount = priv->height_block_size;
	} else {
		ycount = priv->content_rect.height / (gdouble)priv->row_count;
	}
	block_height = priv->cur_block_height;
	cairo_set_antialias(priv->fg_cairo, CAIRO_ANTIALIAS_NONE);
	cairo_set_antialias(priv->hl_cairo, CAIRO_ANTIALIAS_NONE);
	/*
	 * Render the contents for the various blocks.
	 */
	for (ix = 0; ix < xcount; ix++) {
		for (iy = 0; iy < ycount; iy++) {
			alpha = g_random_double_range(0., 1.);
			//alpha = ix / (gfloat)xcount;
			/*
			 * Clear existing content.
			 */
			cairo_rectangle(priv->fg_cairo,
			                GDK_RECTANGLE_RIGHT(area) - (ix * block_width) - block_width,
			                GDK_RECTANGLE_BOTTOM(area) - (iy * block_height) - block_height,
			                block_width,
			                block_height);
			cairo_set_operator(priv->fg_cairo, CAIRO_OPERATOR_CLEAR);
			cairo_fill(priv->fg_cairo);
			cairo_rectangle(priv->hl_cairo,
			                GDK_RECTANGLE_RIGHT(area) - (ix * block_width) - block_width,
			                GDK_RECTANGLE_BOTTOM(area) - (iy * block_height) - block_height,
			                block_width,
			                block_height);
			cairo_set_operator(priv->hl_cairo, CAIRO_OPERATOR_CLEAR);
			cairo_fill(priv->hl_cairo);
			cairo_set_operator(priv->fg_cairo, CAIRO_OPERATOR_OVER);
			cairo_set_operator(priv->hl_cairo, CAIRO_OPERATOR_OVER);
			/*
			 * Render the foreground.
			 */
			cairo_rectangle(priv->fg_cairo,
			                GDK_RECTANGLE_RIGHT(area) - (ix * block_width) - block_width,
			                GDK_RECTANGLE_BOTTOM(area) - (iy * block_height) - block_height,
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
			                GDK_RECTANGLE_RIGHT(area) - (ix * block_width) - block_width,
			                GDK_RECTANGLE_BOTTOM(area) - (iy * block_height) - block_height,
			                block_width,
			                block_height);
			cairo_set_source_rgba(priv->hl_cairo,
			                      hl_color.red / 65535.,
			                      hl_color.green / 65535.,
			                      hl_color.blue / 65535.,
			                      alpha);
			cairo_fill(priv->hl_cairo);
		}
		if (!full_draw) {
			break;
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
	rect->x = priv->content_rect.x + 1 + (priv->active_column * priv->cur_block_width);
	rect->y = priv->content_rect.y + 1 + (priv->active_row * priv->cur_block_height);
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
	 * Clear the background.
	 */
	cairo_rectangle(priv->bg_cairo,
	                priv->content_rect.x + .5,
	                priv->content_rect.y + .5,
	                priv->content_rect.width - 1.,
	                priv->content_rect.height - 1.);
	gdk_cairo_set_source_color(priv->bg_cairo, &style->light[GTK_STATE_NORMAL]);
	cairo_fill_preserve(priv->bg_cairo);
	/*
	 * Render the content border.
	 */
	gdk_cairo_set_source_color(priv->bg_cairo, &style->fg[GTK_STATE_NORMAL]);
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
		uber_heat_map_render_fg(UBER_HEAT_MAP(widget), priv->full_draw);
		priv->fg_dirty = FALSE;
		priv->full_draw = FALSE;
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
		priv->col_count = width;
	}
	if (height_is_count) {
		priv->cur_block_height = (priv->content_rect.height - 2) / (gdouble)height;
	} else {
		priv->cur_block_height = height;
		priv->row_count = height;
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
 * uber_heat_map_append:
 * @map: A #UberHeatMap.
 * @values: (element-type double): A #GAarray.
 *
 * Adds the set of values to the circular buffer.  The values are
 * calculated and inserted into the proper buckets.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_append (UberHeatMap *map,    /* IN */
                      GArray      *values) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	g_ring_append_val(priv->ring, values);
	priv->fg_dirty = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(map));
}

/**
 * uber_heat_map_fps_timeout:
 * @map: A #UberHeatMap.
 *
 * Timeout to tick the graph to the next frame.
 *
 * Returns: %TRUE always.
 * Side effects: None.
 */
static gboolean
uber_heat_map_fps_timeout (UberHeatMap *map) /* IN */
{
	UberHeatMapPrivate *priv;
	GArray *values = NULL;

	g_return_val_if_fail(UBER_IS_HEAT_MAP(map), FALSE);

	priv = map->priv;
	priv->fps_off++;
	if (G_UNLIKELY(priv->fps_off >= priv->fps_calc)) {
		if (!uber_heat_map_get_next_values(map, &values)) {
			values = NULL;
		}
		uber_heat_map_append(map, values);
		priv->fps_off = 0;
	}
	return TRUE;
}

/**
 * uber_heat_map_set_fps:
 * @map: A #UberHeatMap.
 * @fps: The frames-per-second to achieve.
 *
 * Sets the target number of Frames Per Second that the graph should try to
 * achieve.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_heat_map_set_fps (UberHeatMap *map, /* IN */
                       gint         fps) /* IN */
{
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(fps > 0);

	priv = map->priv;
	priv->fps = fps;
	priv->fps_calc = fps;
	priv->fps_to = 1000. / fps;
	priv->fps_each = (gfloat)priv->content_rect.width /
	                 (gfloat)priv->stride /
	                 (gfloat)priv->fps;
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
	/*
	 * If we are moving less than one pixel per frame, then go ahead and lower
	 * the actual framerate and move 1 pixel at a time.
	 */
	if (priv->fps_each < 1.) {
		priv->fps_each = 1.;
		priv->fps_calc = (gfloat)priv->content_rect.width / (gfloat)priv->stride;
		priv->fps_to = 1000. / priv->fps_calc;
	}
	priv->fps_handler = g_timeout_add(priv->fps_to,
	                                  (GSourceFunc)uber_heat_map_fps_timeout,
	                                  map);
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
	uber_heat_map_set_fps(UBER_HEAT_MAP(widget), priv->fps);
	priv->bg_dirty = TRUE;
	priv->fg_dirty = TRUE;
	priv->full_draw = TRUE;
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
	/*
	 * Check if we are hovering a box in the view.
	 */
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
	/*
	 * Update the highlight box if necessary.
	 */
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
	req->width = 150;
	req->height = 50;
}

/**
 * uber_heat_map_destroy_array:
 * @data: A pointer to a #GArray.
 *
 * Frees a GArray after it has been released from the #GRing.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_heat_map_destroy_array (gpointer data) /* IN */
{
	GArray **ar = data;

	g_return_if_fail(data != NULL);

	if (*ar) {
		g_array_unref(*ar);
	}
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
	UberHeatMapPrivate *priv;

	g_return_if_fail(UBER_IS_HEAT_MAP(object));

	priv = UBER_HEAT_MAP(object)->priv;
	uber_heat_map_destroy_drawables(UBER_HEAT_MAP(object));
	if (priv->fps_handler) {
		g_source_remove(priv->fps_handler);
	}
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
	priv->stride = 60; /* TODO: Allow to be changed */
	priv->ring = g_ring_sized_new(sizeof(GArray*), priv->stride,
	                              uber_heat_map_destroy_array);
	uber_heat_map_set_block_size(map, 20, TRUE, 10, TRUE);
	/*
	 * Enable required GdkEvents.
	 */
	mask |= GDK_ENTER_NOTIFY_MASK;
	mask |= GDK_LEAVE_NOTIFY_MASK;
	mask |= GDK_POINTER_MOTION_MASK;
	gtk_widget_set_events(GTK_WIDGET(map), mask);
	/*
	 * Setup callback to retrieve next set of values.
	 */
	uber_heat_map_set_fps(map, 20);
}
