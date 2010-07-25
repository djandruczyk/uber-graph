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

#define ACTOR_CLASS (CLUTTER_ACTOR_CLASS(uber_graph_parent_class))

/**
 * SECTION:uber-graph.h
 * @title: UberGraph
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberGraph, uber_graph, CLUTTER_TYPE_ACTOR)

struct _UberGraphPrivate
{
	ClutterActor    *bg_actor;     /* Background including grid. */
	ClutterActor    *fg_actor;     /* Foreground content view. */
	ClutterActor    *title;        /* Graph title. */
	gboolean         fg_dirty;     /* Need redraw of foreground. */
	gboolean         bg_dirty;     /* Need redraw of background. */
	gboolean         alloc_dirty;  /* Cairo textures need surface resized */
	ClutterActorBox  content_rect; /* Content rectangle area. */
	gint             x_slots;      /* How many X axis data slots. */
	gfloat           x_each;       /* How much to move during each slot. */
};

/**
 * clutter_actor_box_size_equal:
 * @a: A #ClutterActorBox.
 * @b: A #ClutterActorBox.
 *
 * Checks to see if the size of the boxes @a and @b are equal.  This ignores
 * the origin of the boxes.
 *
 * Returns: %TRUE if the sizes are equal; otherwise %FALSE.
 * Side effects: None.
 */
static inline gboolean
clutter_actor_box_size_equal (const ClutterActorBox *a, /* IN */
                              const ClutterActorBox *b) /* IN */
{
	if (clutter_actor_box_get_width(a) == clutter_actor_box_get_width(b)) {
		if (clutter_actor_box_get_height(a) == clutter_actor_box_get_height(b)) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * uber_graph_new:
 *
 * Creates a new instance of #UberGraph.
 *
 * Returns: the newly created instance of #UberGraph.
 * Side effects: None.
 */
ClutterActor*
uber_graph_new (void)
{
	UberGraph *graph;

	graph = g_object_new(UBER_TYPE_GRAPH, NULL);
	return CLUTTER_ACTOR(graph);
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
	ClutterActorBox box;
	ClutterActorBox title_box;

	g_return_if_fail(UBER_IS_GRAPH(graph));

	priv = graph->priv;
	/*
	 * Retrieve allocation areas for which to base our positions upon.
	 */
	clutter_actor_get_allocation_box(CLUTTER_ACTOR(graph), &box);
	clutter_actor_get_allocation_box(priv->title, &title_box);
	/*
	 * Calculate the content area.
	 */
	priv->content_rect.x1 = box.x1
	                      + clutter_actor_box_get_height(&title_box)
	                      + 1.5;
	priv->content_rect.y1 = box.y1 + 1.5;
	priv->content_rect.x2 = box.x2 - 1.5;
	priv->content_rect.y2 = box.y2 - 1.5;
	/*
	 * Calculate how much to move between each content update.
	 */
	priv->x_each = clutter_actor_box_get_width(&priv->content_rect)
	             / (gfloat)priv->x_slots;
}

/**
 * uber_graph_allocate:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_allocate (ClutterActor           *actor, /* IN */
                     const ClutterActorBox  *box,   /* IN */
                     ClutterAllocationFlags  flags) /* IN */
{
	UberGraphPrivate *priv;
	ClutterActorBox title_box;
	ClutterActorBox old_box;
	gfloat width;
	gfloat height;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	ACTOR_CLASS->allocate(actor, box, flags);
	/*
	 * Update allocation for foreground.  Note if the size changed so we can
	 * redraw.
	 */
	clutter_actor_get_allocation_box(priv->fg_actor, &old_box);
	priv->fg_dirty = !clutter_actor_box_size_equal(box, &old_box);
	clutter_actor_allocate(priv->fg_actor, box, flags);
	/*
	 * Update allocation for background.  Note if the size changed so we can
	 * redraw.
	 */
	clutter_actor_get_allocation_box(priv->bg_actor, &old_box);
	priv->bg_dirty = !clutter_actor_box_size_equal(box, &old_box);
	clutter_actor_allocate(priv->bg_actor, box, flags);
	/*
	 * Note of the allocation changed so we can resize the textures.
	 */
	priv->alloc_dirty = priv->bg_dirty || priv->fg_dirty;
	/*
	 * Move the title into its new location.
	 */
	clutter_actor_get_preferred_size(priv->title, NULL, NULL, &width, &height);
	title_box.x1 = box->x1;
	title_box.y1 = ((clutter_actor_get_height(actor) - width) / 2.) + width;
	title_box.x2 = title_box.x1 + width;
	title_box.y2 = title_box.y1 + height;
	clutter_actor_allocate(priv->title, &title_box, flags);
	/*
	 * Recalculate regions for various drawings.
	 */
	uber_graph_calculate_rects(UBER_GRAPH(actor));
}

/**
 * uber_graph_parent_set:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_parent_set (ClutterActor *actor,      /* IN */
                       ClutterActor *old_parent) /* IN */
{
	UberGraphPrivate *priv;
	ClutterActor *parent;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	parent = clutter_actor_get_parent(actor);
	clutter_actor_reparent(priv->fg_actor, parent);
	clutter_actor_reparent(priv->bg_actor, parent);
	clutter_actor_reparent(priv->title, parent);
}

/**
 * uber_graph_show:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_show (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	ACTOR_CLASS->show(actor);
	clutter_actor_show(priv->fg_actor);
	clutter_actor_show(priv->bg_actor);
	clutter_actor_show(priv->title);
	/*
	 * Start the animation sequence to slide the foreground from
	 * right to left.
	 */
#if 1
	clutter_actor_animate(priv->fg_actor, CLUTTER_LINEAR, 10000,
	                      "x", clutter_actor_get_x(actor) - 100.,
	                      NULL);
#endif
}

/**
 * uber_graph_hide:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_hide (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	ACTOR_CLASS->hide(actor);
	clutter_actor_hide(priv->fg_actor);
	clutter_actor_hide(priv->bg_actor);
	clutter_actor_hide(priv->title);
}

/**
 * uber_graph_map:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_map (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	ACTOR_CLASS->map(actor);
	clutter_actor_map(priv->bg_actor);
	clutter_actor_map(priv->fg_actor);
	clutter_actor_map(priv->title);
}

/**
 * uber_graph_unmap:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_unmap (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	ACTOR_CLASS->unmap(actor);
	clutter_actor_unmap(priv->bg_actor);
	clutter_actor_unmap(priv->fg_actor);
	clutter_actor_unmap(priv->title);
}

/**
 * uber_graph_paint_background:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_paint_background (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;
	const gdouble dashes[] = { 1.0, 2.0 };
	ClutterColor bg_color = { 0xba, 0xbd, 0xb6, 0xff };
	gfloat width;
	gfloat height;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	g_debug("PAINTING BACKGROUND");

	priv = UBER_GRAPH(actor)->priv;
	clutter_actor_get_size(priv->bg_actor, &width, &height);
	/*
	 * Create cairo context for drawing.
	 */
	cr = clutter_cairo_texture_create(CLUTTER_CAIRO_TEXTURE(priv->bg_actor));
	/*
	 * Clear the background.
	 */
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_fill(cr);
	cairo_restore(cr);
	/*
	 * Set the background color if needed.
	 */
	if (TRUE) {
		cairo_save(cr);
		cairo_rectangle(cr, 0, 0, width, height);
		clutter_cairo_set_source_color(cr, &bg_color);
		cairo_fill(cr);
		cairo_restore(cr);
	}
	/*
	 * Draw the content background and border.
	 */
	cairo_save(cr);
	cairo_rectangle(cr, priv->content_rect.x1, priv->content_rect.y1,
	                clutter_actor_box_get_width(&priv->content_rect),
	                clutter_actor_box_get_height(&priv->content_rect));
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_fill_preserve(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_line_width(cr, 1.0);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	clutter_cairo_set_source_color(cr, &bg_color);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_dash(cr, dashes, G_N_ELEMENTS(dashes), 0);
	cairo_stroke(cr);
	cairo_restore(cr);
	/*
	 * Clean up resources and send texture to the GL texture.
	 */
	cairo_destroy(cr);
	/*
	 * Mark the background clean.
	 */
	priv->bg_dirty = FALSE;
}

/**
 * uber_graph_paint_foreground:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_paint_foreground (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;
	ClutterActorBox box;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	g_debug("PAINTING FOREGROUND");
	/*
	 * XXX: Temporary hack to show some random data while we get
	 *   scrolling in place.
	 */
	if (TRUE) {
		cairo_t *cr;
		gfloat each;
		gint i;

		clutter_actor_get_allocation_box(priv->fg_actor, &box);
		each = clutter_actor_box_get_width(&box) / (gfloat)priv->x_slots;
		cr = clutter_cairo_texture_create(CLUTTER_CAIRO_TEXTURE(priv->fg_actor));
		for (i = 0; i < priv->x_slots; i++) {
			cairo_rectangle(cr,
			                box.x1 + (each * i),
			                box.y1,
			                each,
			                clutter_actor_box_get_height(&box));
			cairo_set_source_rgb(cr,
			                     i / (gfloat)priv->x_slots,
			                     i / (gfloat)priv->x_slots,
			                     i / (gfloat)priv->x_slots);
			cairo_fill(cr);
		}
		cairo_destroy(cr);
	}
	/*
	 * Mark the foreground clean.
	 */
	priv->fg_dirty = FALSE;
}

/**
 * uber_graph_paint:
 * @actor: A #ClutterActor.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_paint (ClutterActor *actor) /* IN */
{
	UberGraphPrivate *priv;
	gfloat width;
	gfloat height;

	g_return_if_fail(UBER_IS_GRAPH(actor));

	priv = UBER_GRAPH(actor)->priv;
	clutter_actor_get_size(actor, &width, &height);
	/*
	 * Resize surface area if needed.  This is done here rather than in the
	 * allocation cycle because it will queue a relayout which is generally
	 * a bad idea during the allocation cycle.
	 */
	if (priv->alloc_dirty) {
		clutter_cairo_texture_set_surface_size(CLUTTER_CAIRO_TEXTURE(priv->fg_actor),
		                                       width, height);
		clutter_cairo_texture_set_surface_size(CLUTTER_CAIRO_TEXTURE(priv->bg_actor),
		                                       width, height);
		priv->alloc_dirty = FALSE;
		priv->fg_dirty = TRUE;
		priv->bg_dirty = TRUE;
	}
	/*
	 * Redraw the background if needed.
	 */
	if (priv->bg_dirty) {
		uber_graph_paint_background(actor);
	}
	/*
	 * Redraw the foreground if needed.
	 */
	if (priv->fg_dirty) {
		uber_graph_paint_foreground(actor);
	}
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
 * @object: An #UberGraph.
 *
 * XXX
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_graph_dispose (GObject *object) /* IN */
{
	UberGraphPrivate *priv;

	g_return_if_fail(UBER_IS_GRAPH(object));

	priv = UBER_GRAPH(object)->priv;
	g_object_unref(priv->fg_actor);
	g_object_unref(priv->bg_actor);
	priv->fg_actor = NULL;
	priv->bg_actor = NULL;
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
	ClutterActorClass *actor_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_graph_finalize;
	object_class->dispose = uber_graph_dispose;
	g_type_class_add_private(object_class, sizeof(UberGraphPrivate));

	actor_class = CLUTTER_ACTOR_CLASS(klass);
	actor_class->allocate = uber_graph_allocate;
	actor_class->hide = uber_graph_hide;
	actor_class->map = uber_graph_map;
	actor_class->paint = uber_graph_paint;
	actor_class->parent_set = uber_graph_parent_set;
	actor_class->show = uber_graph_show;
	actor_class->unmap = uber_graph_unmap;
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

	graph->priv = G_TYPE_INSTANCE_GET_PRIVATE(graph,
	                                          UBER_TYPE_GRAPH,
	                                          UberGraphPrivate);
	priv = graph->priv;
	priv->x_slots = 60;
	/*
	 * Create foreground and background cairo textures with 1x1 pixel
	 * surfaces.  The surface will be resized once we have an allocation
	 * set.
	 */
	priv->fg_actor = clutter_cairo_texture_new(1, 1);
	priv->bg_actor = clutter_cairo_texture_new(1, 1);
	/*
	 * Setup the graph title.
	 */
	priv->title = clutter_text_new();
	clutter_text_set_text(CLUTTER_TEXT(priv->title), "Uber Graph");
	clutter_text_set_font_name(CLUTTER_TEXT(priv->title), "Sans 16pt Bold");
	clutter_actor_set_rotation(priv->title, CLUTTER_Z_AXIS,
	                           270., 0., 0., 0.);
}
