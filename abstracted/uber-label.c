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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>

#include "uber-label.h"

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

enum
{
	COLOR_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

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

	label = g_object_new(UBER_TYPE_LABEL, NULL);
	return GTK_WIDGET(label);
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

	priv = label->priv;
	gtk_label_set_text(GTK_LABEL(priv->label), text);
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

	priv = label->priv;
	priv->color = *color;
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

	priv = label->priv;
	priv->in_block = TRUE;
	gtk_widget_queue_draw(widget);
	return FALSE;
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

	priv = label->priv;
	priv->in_block = FALSE;
	gtk_widget_queue_draw(widget);
	return FALSE;
}

/**
 * uber_label_block_button_press_event:
 * @widget: A #GtkWidget.
 * @event: A #GdkEventButton.
 * @label: An #UberLabel.
 *
 * Callback to handle a button press event within the colored block.
 *
 * Returns: %FALSE always to allow further signal emission.
 * Side effects: None.
 */
static gboolean
uber_label_block_button_press_event (GtkWidget      *widget, /* IN */
                                     GdkEventButton *event,  /* IN */
                                     UberLabel      *label)  /* IN */
{
	UberLabelPrivate *priv;
	GtkWidget *dialog;
	GtkWidget *selection;

	g_return_val_if_fail(UBER_IS_LABEL(label), FALSE);

	priv = label->priv;
	dialog = gtk_color_selection_dialog_new("");
	selection = gtk_color_selection_dialog_get_color_selection(
			GTK_COLOR_SELECTION_DIALOG(dialog));
	gtk_color_selection_set_current_color(
			GTK_COLOR_SELECTION(selection),
			&priv->color);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		gtk_color_selection_get_current_color(
				GTK_COLOR_SELECTION(selection),
				&priv->color);
		gtk_widget_queue_draw(widget);
		g_signal_emit(label, signals[COLOR_CHANGED],
		              0, &priv->color);
	}
	gtk_widget_destroy(dialog);
	return FALSE;
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
	G_OBJECT_CLASS(uber_label_parent_class)->finalize(object);
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

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_label_finalize;
	g_type_class_add_private(object_class, sizeof(UberLabelPrivate));

	/**
	 * UberLabel::color-changed:
	 * @label: An #UberLabel.
	 * @color: A #GdkColor.
	 *
	 * Signal emitted when the color is changed.
	 */
	signals[COLOR_CHANGED] = g_signal_new("color-changed",
	                                      UBER_TYPE_LABEL,
	                                      G_SIGNAL_RUN_FIRST,
	                                      0,
	                                      NULL,
	                                      NULL,
	                                      g_cclosure_marshal_VOID__POINTER,
	                                      G_TYPE_NONE,
	                                      1,
	                                      G_TYPE_POINTER);
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
	                      GDK_ENTER_NOTIFY_MASK |
	                      GDK_LEAVE_NOTIFY_MASK |
	                      GDK_BUTTON_PRESS_MASK);
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
	g_signal_connect(priv->block,
	                 "button-press-event",
	                 G_CALLBACK(uber_label_block_button_press_event),
	                 label);
	gtk_widget_set_tooltip_text(GTK_WIDGET(priv->block),
	                            _("Click to select color"));
	gtk_widget_show(priv->hbox);
	gtk_widget_show(priv->block);
	gtk_widget_show(priv->label);
}
