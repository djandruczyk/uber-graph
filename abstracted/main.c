#include "uber-line-graph.h"

static gboolean
next_data_func (UberLineGraph *graph,     /* IN */
                gdouble       *value,     /* OUT */
                gpointer       user_data) /* IN */
{
	*value = 15;
	return TRUE;
}

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	GtkWidget *window;
	GtkWidget *graph;

	gtk_init(&argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	graph = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(graph),
	                              next_data_func, NULL, NULL);
	gtk_container_add(GTK_CONTAINER(window), graph);
	gtk_widget_show(graph);
	gtk_widget_show(window);
	g_signal_connect(window,
	                 "delete-event",
	                 G_CALLBACK(gtk_main_quit),
	                 NULL);
	gtk_main();
	return 0;
}
