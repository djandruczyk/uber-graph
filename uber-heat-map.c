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
	GdkRectangle  content_rect;
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

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
	drawable = gtk_widget_get_window(GTK_WIDGET(map));
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
	/*
	 * Create server-side pixmaps.
	 */
	priv->bg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	priv->fg_pixmap = gdk_pixmap_new(drawable, alloc.width, alloc.height, -1);
	/*
	 * Setup cairo.
	 */
	priv->bg_cairo = gdk_cairo_create(priv->bg_pixmap);
	priv->fg_cairo = gdk_cairo_create(priv->fg_pixmap);
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
	cairo_destroy(priv->bg_cairo);
	cairo_destroy(priv->fg_cairo);
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

	g_return_if_fail(UBER_IS_HEAT_MAP(map));


	priv = map->priv;
	gtk_widget_get_allocation(GTK_WIDGET(map), &alloc);
	g_message("Calculating %d,%d", alloc.width, alloc.height);
	/*
	 * Calculate main content area.
	 */
	priv->content_rect.x = 1.5;
	priv->content_rect.y = 1.5;
	priv->content_rect.width = alloc.width - 3.;
	priv->content_rect.height = alloc.height - 3.;
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

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = map->priv;
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

	g_return_if_fail(UBER_IS_HEAT_MAP(widget));

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
	 * Cleanup after drawing.
	 */
	cairo_destroy(cr);
	return FALSE;
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

	g_return_if_fail(UBER_IS_HEAT_MAP(map));

	priv = UBER_HEAT_MAP(widget)->priv;
	WIDGET->size_allocate(widget, alloc);
	uber_heat_map_calculate_rects(UBER_HEAT_MAP(widget));
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
}
