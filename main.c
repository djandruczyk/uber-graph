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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "UberGraph"
#endif

#ifndef LOCALE_DIR
#define LOCALE_DIR "/usr/share/locale"
#endif

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "uber-graph.h"

static GOptionEntry options[] = {
	{ NULL }
};

static const gdouble data_set[][2] = {
	{ 1277362703, 161 },
	{ 1277362704, 89 },
	{ 1277362705, 68 },
	{ 1277362706, 161 },
	{ 1277362707, 48 },
	{ 1277362708, 176 },
	{ 1277362709, 142 },
	{ 1277362710, 129 },
	{ 1277362711, 163 },
	{ 1277362712, 87 },
	{ 1277362713, 188 },
	{ 1277362714, 194 },
	{ 1277362715, 125 },
	{ 1277362716, 187 },
	{ 1277362717, 68 },
	{ 1277362718, 103 },
	{ 1277362719, 18 },
	{ 1277362720, 163 },
	{ 1277362721, 189 },
	{ 1277362722, 113 },
	{ 1277362723, 56 },
	{ 1277362724, 165 },
	{ 1277362725, 154 },
	{ 1277362726, 78 },
	{ 1277362727, 83 },
	{ 1277362728, 71 },
	{ 1277362729, 195 },
	{ 1277362730, 118 },
	{ 1277362731, 154 },
	{ 1277362732, 101 },
	{ 1277362733, 187 },
	{ 1277362734, 170 },
	{ 1277362735, 85 },
	{ 1277362736, 122 },
	{ 1277362737, 20 },
	{ 1277362738, 61 },
	{ 1277362739, 144 },
	{ 1277362740, 51 },
	{ 1277362741, 22 },
	{ 1277362742, 109 },
	{ 1277362743, 139 },
	{ 1277362744, 77 },
	{ 1277362745, 182 },
	{ 1277362746, 105 },
	{ 1277362747, 88 },
	{ 1277362748, 191 },
	{ 1277362749, 73 },
	{ 1277362750, 5 },
	{ 1277362751, 122 },
	{ 1277362752, 94 },
	{ 1277362753, 166 },
};

static GtkWidget*
create_main_window (void)
{
	GtkWidget *window;
	GtkWidget *graph;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 12);
	gtk_window_set_title(GTK_WINDOW(window), _("UberGraph"));
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 200);
	gtk_widget_show(window);
	graph = uber_graph_new();
	uber_graph_set_data(UBER_GRAPH(graph), (const gdouble **)data_set,
	                    G_N_ELEMENTS(data_set));
	gtk_container_add(GTK_CONTAINER(window), graph);
	gtk_widget_show(graph);
	return window;
}

gint
main (gint   argc,
      gchar *argv[])
{
	GOptionContext *context;
	GError *error = NULL;
	GtkWidget *window;

	/* initialize i18n */
	textdomain(GETTEXT_PACKAGE);
	bindtextdomain(GETTEXT_PACKAGE, LOCALE_DIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	g_set_application_name(_("uber-graph"));

	/* initialize threading early */
	g_thread_init(NULL);

	/* parse command line arguments */
	context = g_option_context_new(_("- realtime graph prototype"));
	g_option_context_add_main_entries(context, options, GETTEXT_PACKAGE);
	g_option_context_add_group(context, gtk_get_option_group(TRUE));
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("%s\n", error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	window = create_main_window();
	g_signal_connect(window, "delete-event", gtk_main_quit, NULL);
	gtk_main();

	return EXIT_SUCCESS;
}
