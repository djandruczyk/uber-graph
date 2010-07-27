#include "uber-line-graph.h"
#include "uber-heat-map.h"

static guint gdk_event_count = 0;

static void
my_gdk_event_func (GdkEvent *event, /* IN */
                   gpointer  data)  /* IN */
{
	gdk_event_count++;
	gtk_main_do_event(event);
}

static gboolean
next_data_func (UberLineGraph *graph,     /* IN */
                gdouble       *value,     /* OUT */
                gpointer       user_data) /* IN */
{
	*value = gdk_event_count;
	gdk_event_count = 0;
	return TRUE;
}

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *line;
	GtkWidget *map;

	gtk_init(&argc, &argv);
	/*
	 * Install event hook to track how many X events we are doing.
	 */
	gdk_event_handler_set(my_gdk_event_func, NULL, NULL);
	/*
	 * Create window.
	 */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	vbox = gtk_vbox_new(TRUE, 3);
	line = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	map = g_object_new(UBER_TYPE_HEAT_MAP, NULL);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(line),
	                              next_data_func, NULL, NULL);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), line, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), map, TRUE, TRUE, 0);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 300);
	gtk_widget_show(map);
	gtk_widget_show(line);
	gtk_widget_show(vbox);
	gtk_widget_show(window);
	g_signal_connect(window,
	                 "delete-event",
	                 G_CALLBACK(gtk_main_quit),
	                 NULL);
	gtk_main();
	return 0;
}
