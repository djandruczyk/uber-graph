#include <clutter/clutter.h>
#include "uber-graph.h"

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	ClutterActor *stage;
	ClutterActor *graph;

	clutter_init(&argc, &argv);

	stage = clutter_stage_get_default();
	graph = uber_graph_new();
	clutter_container_add_actor(CLUTTER_CONTAINER(stage), graph);
	clutter_actor_set_position(graph, 0, 0);
	clutter_actor_set_size(graph,
	                       clutter_actor_get_width(stage),
	                       clutter_actor_get_height(stage));
	clutter_actor_show(graph);
	clutter_actor_show(stage);
	clutter_main();

	return 0;
}
