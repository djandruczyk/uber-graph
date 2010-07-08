/* uber-label.c
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

#include "uber-label.h"

#ifdef UBER_TRACE
#define TRACE(_f,...) \
    G_STMT_START { \
        g_log(G_LOG_DOMAIN, 1 << G_LOG_LEVEL_USER_SHIFT, _f, ## __VA_ARGS__); \
    } G_STMT_END
#define ENTRY TRACE("ENTRY: %s():%d", G_STRFUNC, __LINE__)
#define EXIT \
    G_STMT_START { \
        TRACE(" EXIT: %s():%d", G_STRFUNC, __LINE__); \
        return; \
    } G_STMT_END
#define RETURN(_r) \
    G_STMT_START { \
        TRACE(" EXIT: %s():%d", G_STRFUNC, __LINE__); \
        return (_r); \
	} G_STMT_END
#define GOTO(_l) \
    G_STMT_START { \
        TRACE(" GOTO: %s():%d %s", G_STRFUNC, __LINE__, #_l); \
        goto _l; \
	} G_STMT_END
#define CASE(_l) \
    case _l: \
        TRACE(" CASE: %s():%d %s", G_STRFUNC, __LINE__, #_l)
#else
#define ENTRY
#define EXIT       return
#define RETURN(_r) return (_r)
#define GOTO(_l)   goto _l
#define CASE(_l)   case _l:
#endif

/**
 * SECTION:uber-label.h
 * @title: UberLabel
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberLabel, uber_label, GTK_TYPE_ALIGNMENT)

struct _UberLabelPrivate
{
	GtkWidget *hbox;
	GtkWidget *block;
	GtkWidget *label;
	GdkColor   color;
	gboolean   in_block;
};

/**
 * uber_label_new:
 *
 * Creates a new instance of #UberLabel.
 *
 * Returns: the newly created instance of #UberLabel.
 * Side effects: None.
 */
GtkWidget*
uber_label_new (void)
{
	UberLabel *label;

	ENTRY;
	label = g_object_new(UBER_TYPE_LABEL, NULL);
	RETURN(GTK_WIDGET(label));
}

/**
 * uber_label_set_text:
 * @label: A #UberLabel.
 * @text: The label text.
 *
 * Sets the text for the label.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_label_set_text (UberLabel   *label, /* IN */
                     const gchar *text)  /* IN */
{
	UberLabelPrivate *priv;

	g_return_if_fail(UBER_IS_LABEL(label));

	ENTRY;
	priv = label->priv;
	gtk_label_set_text(GTK_LABEL(priv->label), text);
	EXIT;
}

/**
 * uber_label_set_color:
 * @label: A #UberLabel.
 * @color: A #GdkColor.
 *
 * Sets the color of the label.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_label_set_color (UberLabel      *label, /* IN */
                      const GdkColor *color) /* IN */
{
	UberLabelPrivate *priv;

	g_return_if_fail(UBER_IS_LABEL(label));

	ENTRY;
	priv = label->priv;
	priv->color = *color;
	EXIT;
}

static void
uber_label_block_expose_event (GtkWidget      *block, /* IN */
                               GdkEventExpose *event, /* IN */
                               UberLabel      *label) /* IN */
{
	UberLabelPrivate *priv;
	GtkAllocation alloc;
	cairo_t *cr;

	g_return_if_fail(UBER_IS_LABEL(label));

	ENTRY;
	priv = label->priv;
	gtk_widget_get_allocation(block, &alloc);
	cr = gdk_cairo_create(event->window);
	/*
	 * Clip drawing region.
	 */
	gdk_cairo_rectangle(cr, &event->area);
	cairo_clip(cr);
	/*
	 * Draw background.
	 */
	gdk_cairo_set_source_color(cr, &priv->color);
	cairo_rectangle(cr, .5, .5, alloc.width - 1., alloc.height - 1.);
	cairo_fill_preserve(cr);
	/*
	 * Add highlight if mouse is in the block.
	 */
	if (priv->in_block) {
		cairo_set_source_rgba(cr, 1., 1., 1., .3);
		cairo_fill_preserve(cr);
	}
	/*
	 * Stroke the edge of the block.
	 */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0., 0., 0., .5);
	cairo_stroke(cr);
	/*
	 * Stroke the highlight of the block.
	 */
	cairo_rectangle(cr, 1.5, 1.5, alloc.width - 3., alloc.height - 3.);
	cairo_set_source_rgba(cr, 1., 1., 1., .5);
	cairo_stroke(cr);
	EXIT;
}

/**
 * uber_label_block_enter_notify_event:
 * @label: A #UberLabel.
 *
 * Tracks the mouse entering the block widget.
 *
 * Returns: %FALSE to allow further callbacks.
 * Side effects: None.
 */
static gboolean
uber_label_block_enter_notify_event (GtkWidget        *widget, /* IN */
                                     GdkEventCrossing *event,  /* IN */
                                     UberLabel        *label)  /* IN */
{
	UberLabelPrivate *priv;

	ENTRY;
	priv = label->priv;
	priv->in_block = TRUE;
	gtk_widget_queue_draw(widget);
	RETURN(FALSE);
}

/**
 * uber_label_block_leave_notify_event:
 * @label: A #UberLabel.
 *
 * Tracks the mouse leaving the block widget.
 *
 * Returns: %FALSE to allow further callbacks.
 * Side effects: None.
 */
static gboolean
uber_label_block_leave_notify_event (GtkWidget        *widget, /* IN */
                                     GdkEventCrossing *event,  /* IN */
                                     UberLabel        *label)  /* IN */
{
	UberLabelPrivate *priv;

	ENTRY;
	priv = label->priv;
	priv->in_block = FALSE;
	gtk_widget_queue_draw(widget);
	RETURN(FALSE);
}

/**
 * uber_label_finalize:
 * @object: A #UberLabel.
 *
 * Finalizer for a #UberLabel instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_label_finalize (GObject *object) /* IN */
{
	ENTRY;
	G_OBJECT_CLASS(uber_label_parent_class)->finalize(object);
	EXIT;
}

/**
 * uber_label_class_init:
 * @klass: A #UberLabelClass.
 *
 * Initializes the #UberLabelClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_label_class_init (UberLabelClass *klass) /* IN */
{
	GObjectClass *object_class;

	ENTRY;
	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_label_finalize;
	g_type_class_add_private(object_class, sizeof(UberLabelPrivate));
	EXIT;
}

/**
 * uber_label_init:
 * @label: A #UberLabel.
 *
 * Initializes the newly created #UberLabel instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_label_init (UberLabel *label) /* IN */
{
	UberLabelPrivate *priv;

	ENTRY;
	label->priv = G_TYPE_INSTANCE_GET_PRIVATE(label,
	                                          UBER_TYPE_LABEL,
	                                          UberLabelPrivate);
	priv = label->priv;
	priv->hbox = gtk_hbox_new(FALSE, 6);
	priv->block = gtk_drawing_area_new();
	priv->label = gtk_label_new(NULL);
	gdk_color_parse("#cc0000", &priv->color);
	gtk_misc_set_alignment(GTK_MISC(priv->label), .0, .5);
	gtk_widget_set_size_request(priv->block, 32, 17);
	gtk_container_add(GTK_CONTAINER(label), priv->hbox);
	gtk_box_pack_start(GTK_BOX(priv->hbox), priv->block, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(priv->hbox), priv->label, TRUE, TRUE, 0);
	gtk_widget_add_events(priv->block,
	                      GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect(priv->block,
	                 "expose-event",
	                 G_CALLBACK(uber_label_block_expose_event),
	                 label);
	g_signal_connect(priv->block,
	                 "enter-notify-event",
	                 G_CALLBACK(uber_label_block_enter_notify_event),
	                 label);
	g_signal_connect(priv->block,
	                 "leave-notify-event",
	                 G_CALLBACK(uber_label_block_leave_notify_event),
	                 label);
	gtk_widget_show(priv->hbox);
	gtk_widget_show(priv->block);
	gtk_widget_show(priv->label);
	EXIT;
}
