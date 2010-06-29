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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include "uber-graph.h"
#include "uber-buffer.h"

static GOptionEntry options[] = {
	{ NULL }
};

static GtkWidget *load_graph = NULL;
static GtkWidget *cpu_graph  = NULL;
static GtkWidget *net_graph  = NULL;

static gboolean
next_cpu (gpointer data)
{
	static gboolean initialized = FALSE;
	static gfloat u1, n1, s1, i1;
	gfloat u2, n2, s2, i2;
	gfloat u3, n3, s3, i3;
	gdouble total, percent;
	int fd;
	char buf[1024];

	fd = open("/proc/stat", O_RDONLY);
	if (fd < 0) {
		g_warning("Failed to open /proc/stat");
		return TRUE;
	}

	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	if (sscanf(buf, "cpu %f %f %f %f", &u2, &n2, &s2, &i2) != 4) {
		g_warning("Failed to read cpu line.");
	}

	if (!initialized) {
		initialized = TRUE;
		goto finish;
	}

	u3 = (u2 - u1);
	n3 = (n2 - n1);
	s3 = (s2 - s1);
	i3 = (i2 - i1);

	total = (u3 + n3 + s3 + i3);
	percent = (100 * (u3 + n3 + s3)) / total;

	g_debug("Pushing cpu percent=%f", percent);
	uber_graph_pushv(UBER_GRAPH(cpu_graph), &percent);

  finish:
  	close(fd);
	u1 = u2;
	i1 = i2;
	s1 = s2;
	n1 = n2;
	return TRUE;
}

static gboolean
next_net (gpointer data)
{
	static gboolean initialized = FALSE;
	static gdouble lastTotalIn = 0;
	static gdouble lastTotalOut = 0;
	char buf[4096];
	char iface[32];
	char *line;
	int fd;
	int i;
	int l = 0;
	gulong dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7;
	gulong bytesIn = 0;
	gulong bytesOut = 0;
	gdouble totalIn = 0;
	gdouble totalOut = 0;
	gdouble diff[2] = { 0 };

	if ((fd = open("/proc/net/dev", O_RDONLY)) < 0) {
		g_warning("Failed to open /proc/net/dev");
		return TRUE;
	}

	read(fd, buf, sizeof(buf));
	buf[sizeof(buf)] = '\0';
	line = buf;
	for (i = 0; buf[i]; i++) {
		if (buf[i] == ':') {
			buf[i] = ' ';
		} else if (buf[i] == '\n') {
			buf[i] = '\0';
			if (++l > 2) { // ignore first two lines
				if (sscanf(line, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu", iface, &bytesIn, &dummy1, &dummy2, &dummy3, &dummy4, &dummy5, &dummy6, &dummy7, &bytesOut) != 10) {
					g_warning("Skipping invalid line: %s", line);
				} else {
					totalIn += bytesIn;
					totalOut += bytesOut;
				}
				line = NULL;
			}
			line = &buf[i+1];
		}
	}

	if (!initialized) {
		initialized = TRUE;
		goto finish;
	}

	diff[0] = (totalIn - lastTotalIn);
	diff[1] = (totalOut - lastTotalOut);
	g_debug("Pushing net receive=%f transmit=%f", diff[0], diff[1]);
	uber_graph_pushv(UBER_GRAPH(net_graph), diff);

  finish:
	close(fd);
	lastTotalOut = totalOut;
	lastTotalIn = totalIn;
	return TRUE;
}

static GtkWidget*
create_main_window (void)
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *load_label;
	GtkWidget *cpu_label;
	GtkWidget *net_label;
	UberRange cpu_range = { 0., 100., 100. };

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 12);
	gtk_window_set_title(GTK_WINDOW(window), _("UberGraph"));
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_widget_show(window);

	vbox = gtk_vbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_widget_show(vbox);

	cpu_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(cpu_label), "<b>CPU History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), cpu_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(cpu_label), .0, .5);
	gtk_widget_show(cpu_label);

	cpu_graph = uber_graph_new();
	uber_graph_set_yautoscale(UBER_GRAPH(cpu_graph), FALSE);
	uber_graph_set_yrange(UBER_GRAPH(cpu_graph), &cpu_range);
	uber_graph_add_line(UBER_GRAPH(cpu_graph));
	gtk_box_pack_start(GTK_BOX(vbox), cpu_graph, TRUE, TRUE, 0);
	gtk_widget_show(cpu_graph);

	load_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(load_label), "<b>Load History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), load_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(load_label), .0, .5);
	gtk_widget_show(load_label);

	load_graph = uber_graph_new();
	uber_graph_set_yautoscale(UBER_GRAPH(load_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_add_line(UBER_GRAPH(load_graph));
	gtk_box_pack_start(GTK_BOX(vbox), load_graph, TRUE, TRUE, 0);
	gtk_widget_show(load_graph);

	net_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(net_label), "<b>Network History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), net_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(net_label), .0, .5);
	gtk_widget_show(net_label);

	net_graph = uber_graph_new();
	uber_graph_set_yautoscale(UBER_GRAPH(net_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(net_graph));
	uber_graph_add_line(UBER_GRAPH(net_graph));
	gtk_box_pack_start(GTK_BOX(vbox), net_graph, TRUE, TRUE, 0);
	gtk_widget_show(net_graph);

	next_cpu(NULL);
	next_net(NULL);

	return window;
}

static gboolean
next_data (gpointer data)
{
	static gdouble values[3] = { 0. };
	char buf[1024];
	int fd = open("/proc/loadavg", O_RDONLY);

	read(fd, buf, sizeof(buf));
	sscanf(buf, "%lf %lf %lf", &values[0], &values[1], &values[2]);
	g_debug("Pushing %f %f %f", values[0], values[1], values[2]);
	uber_graph_pushv(UBER_GRAPH(load_graph), values);
	close(fd);
	return TRUE;
}

static gboolean
test_4_foreach (UberBuffer *buffer,
                gdouble     value,
                gpointer    user_data)
{
	static gint count = 0;
	gint v = value;

	g_assert_cmpint(v, ==, 4 - count);
	count++;
	return (value == 1.);
}

static gboolean
test_2_foreach (UberBuffer *buffer,
                gdouble     value,
                gpointer    user_data)
{
	static gint count = 0;
	gint v = value;

	g_assert_cmpint(v, ==, 4 - count);
	count++;
	return (value == 3.);
}

static gboolean
test_2e_foreach (UberBuffer *buffer,
                 gdouble     value,
                 gpointer    user_data)
{
	static gint count = 0;
	gint v = value;

	if (count == 0 || count == 1) {
		g_assert_cmpint(v, ==, 4. - count);
	} else {
		g_assert(value == -INFINITY);
	}
	count++;
	return FALSE;
}

static void
run_buffer_tests (void)
{
	UberBuffer *buf;

	buf = uber_buffer_new();
	g_assert(buf);

	uber_buffer_append(buf, 1.);
	uber_buffer_append(buf, 2.);
	uber_buffer_append(buf, 3.);
	uber_buffer_append(buf, 4.);
	uber_buffer_foreach(buf, test_4_foreach, NULL);

	uber_buffer_set_size(buf, 2);
	g_assert_cmpint(buf->len, ==, 2);
	g_assert_cmpint(buf->pos, ==, 0);
	uber_buffer_foreach(buf, test_2_foreach, NULL);

	uber_buffer_set_size(buf, 32);
	g_assert_cmpint(buf->len, ==, 32);
	g_assert_cmpint(buf->pos, ==, 0);
	uber_buffer_foreach(buf, test_2e_foreach, NULL);
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
	g_set_application_name(_("uber-load_graph"));

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

	/* run the UberBuffer tests */
	run_buffer_tests();

	/* run the test gui */
	window = create_main_window();
	g_signal_connect(window, "delete-event", gtk_main_quit, NULL);
	g_timeout_add_seconds(1, next_data, NULL);
	g_timeout_add_seconds(1, next_cpu, NULL);
	g_timeout_add_seconds(1, next_net, NULL);
	gtk_main();

	return EXIT_SUCCESS;
}
