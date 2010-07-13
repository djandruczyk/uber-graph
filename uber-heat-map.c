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
	cairo_t      *bg_cairo;
	cairo_t      *fg_cairo;
	gboolean      bg_dirty;
	gboolean      fg_dirty;
	gboolean      have_rgba;
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
		gdk_drawable_set_colormap(priv->fg_pixmap, colormap);
	} else {
		priv->fg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	}
	/*
	 * Setup cairo.
	 */
	priv->bg_cairo = gdk_cairo_create(priv->bg_pixmap);
	priv->fg_cairo = gdk_cairo_create(priv->fg_pixmap);
	uber_heat_map_clear_cairo(map, priv->fg_cairo, alloc.width, alloc.height);
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
	gint xcount;
	gint ycount;
	gint ix;
	gint iy;
	gdouble block_width;
	gdouble block_height;
	gdouble alpha;
	GdkColor color;

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	gdk_color_parse("#204a87", &color);
	cairo_save(priv->fg_cairo);
	/*
	 * Calculate the number of x-axis blocks.
	 */
	xcount = 100;
	block_width = priv->content_rect.width / (gfloat)xcount;
	/*
	 * Calculate the number of y-axis blocks.
	 */
	ycount = 10;
	block_height = priv->content_rect.height / (gfloat)ycount;
	/*
	 * Render the contents for the various blocks.
	 */
	for (ix = 0; ix < xcount; ix++) {
		for (iy = 0; iy < ycount; iy++) {
			cairo_rectangle(priv->fg_cairo,
			                priv->content_rect.x + (ix * block_width),
			                priv->content_rect.y + (iy * block_height),
			                block_width,
			                block_height);
			alpha = g_random_double_range(0., 1.);
			cairo_set_source_rgba(priv->fg_cairo,
			                      color.red / 65535.,
			                      color.green / 65535.,
			                      color.blue / 65535.,
			                      alpha);
			cairo_fill(priv->fg_cairo);
		}
	}
	/*
	 * Cleanup after resources.
	 */
	cairo_restore(priv->fg_cairo);
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

	g_return_if_fail(UBER_IS_HEAT_MAP(map));
	g_return_if_fail(width > 0);
	g_return_if_fail(height > 0);

	priv = map->priv;
	/*
	 * Store new width/height block size settings.
	 */
	priv->width_block_size = width;
	priv->width_is_count = width_is_count;
	priv->height_block_size = height;
	priv->height_is_count = height_is_count;
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
	priv->bg_dirty = TRUE;
	priv->fg_dirty = TRUE;
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

	map->priv = G_TYPE_INSTANCE_GET_PRIVATE(map,
	                                        UBER_TYPE_HEAT_MAP,
	                                        UberHeatMapPrivate);
	priv = map->priv;
	priv->tick_len = 10;
}
