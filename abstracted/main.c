#include "uber-line-graph.h"

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	GtkWidget *window;
	GtkWidget *graph;

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	graph = g_object_new(UBER_TYPE_LINE_GRAPH, NULL);
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
