/* main.c
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

#include "uber.h"

static guint gdk_event_count = 0;

static void
gdk_event_hook (GdkEvent *event, /* IN */
                gpointer  data)  /* IN */
{
	gdk_event_count++;
	gtk_main_do_event(event);
}

static gboolean
get_xevent_info (UberLineGraph *graph,     /* IN */
                 guint          line,      /* IN */
                 gdouble       *value,     /* OUT */
                 gpointer       user_data) /* IN */
{
	switch (line) {
	case 1:
		*value = gdk_event_count;
		gdk_event_count = 0;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	GtkWidget *window;
	GtkWidget *line;
	GtkWidget *map;
	GtkWidget *scatter;

	gtk_init(&argc, &argv);
	/*
	 * Install event hook to track how many X events we are doing.
	 */
	gdk_event_handler_set(gdk_event_hook, NULL, NULL);
	/*
	 * Create window.
	 */
	window = uber_window_new();
	line = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	map = g_object_new(UBER_TYPE_HEAT_MAP, NULL);
	scatter = g_object_new(UBER_TYPE_SCATTER, NULL);
	uber_line_graph_add_line(UBER_LINE_GRAPH(line), NULL);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(line),
	                              get_xevent_info, NULL, NULL);
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(line), "X Events");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(map), "IO Latency");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(scatter), "IOPS By Size");
	gtk_widget_show(scatter);
	gtk_widget_show(map);
	gtk_widget_show(line);
	gtk_widget_show(window);
	g_signal_connect(window,
	                 "delete-event",
	                 G_CALLBACK(gtk_main_quit),
	                 NULL);
	gtk_main();
	return 0;
}
