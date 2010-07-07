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
#include <sys/types.h>
#include <signal.h>

#include "uber-graph.h"
#include "uber-buffer.h"

#ifdef DISABLE_DEBUG
#define DEBUG(f,...)
#else
#define DEBUG(f,...) g_debug(f, ## __VA_ARGS__)
#endif

typedef struct
{
	volatile gdouble swapFree;
	volatile gdouble memFree;
} MemInfo;

typedef struct
{
	volatile gdouble cpuUsage;
} CpuInfo;

typedef struct
{
	volatile gdouble bytesIn;
	volatile gdouble bytesOut;
} NetInfo;

typedef struct
{
	volatile gdouble load5;
	volatile gdouble load10;
	volatile gdouble load15;
} LoadInfo;

typedef struct
{
	volatile gdouble size;
	volatile gdouble resident;
} PmemInfo;

typedef struct
{
	volatile gdouble vruntime;
} SchedInfo;

typedef struct
{
	volatile gint n_threads;
} ThreadInfo;

static MemInfo    mem_info   = { 0 };
static CpuInfo    cpu_info   = { 0 };
static NetInfo    net_info   = { 0 };
static LoadInfo   load_info  = { 0 };
static PmemInfo   pmem_info  = { 0 };
static SchedInfo  sched_info = { 0 };
static ThreadInfo thread_info= { 0 };
static GtkWidget *load_graph = NULL;
static GtkWidget *cpu_graph  = NULL;
static GtkWidget *net_graph  = NULL;
static GtkWidget *mem_graph  = NULL;
static gboolean   reaped     = FALSE;
static GtkWidget *vbox       = NULL;
static GtkWidget *pmem_graph = NULL;
static GtkWidget *sched_graph  = NULL;
static GtkWidget *thread_graph = NULL;
static GPid       pid        = 0;

static gboolean
get_cpu (UberGraph *graph,
         gint       line,
         gdouble   *value,
         gpointer   user_data)
{
	*value = cpu_info.cpuUsage;
	return TRUE;
}

static gboolean
get_mem (UberGraph *graph,
         gint       line,
         gdouble   *value,
         gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = mem_info.memFree;
		break;
	case 2:
		*value = mem_info.swapFree;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_load (UberGraph *graph,
          gint       line,
          gdouble   *value,
          gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = load_info.load5;
		break;
	case 2:
		*value = load_info.load10;
		break;
	case 3:
		*value = load_info.load15;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_net (UberGraph *graph,
         gint       line,
         gdouble   *value,
         gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = net_info.bytesIn;
		break;
	case 2:
		*value = net_info.bytesOut;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_threads (UberGraph *graph,
             gint       line,
             gdouble   *value,
             gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = thread_info.n_threads;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static inline GtkWidget*
create_graph (void)
{
	GtkWidget *graph;
	GtkWidget *align;

	graph = uber_graph_new();
	align = gtk_alignment_new(.5, .5, 1., 1.);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 6, 0);
	gtk_container_add(GTK_CONTAINER(align), graph);
	gtk_box_pack_start(GTK_BOX(vbox), align, TRUE, TRUE, 0);
	gtk_widget_show(align);
	gtk_widget_show(graph);
	return graph;
}

static void
next_load (void)
{
	gdouble load5;
	gdouble load10;
	gdouble load15;
	gchar buf[1024];
	gint fd;

	fd = open("/proc/loadavg", O_RDONLY);
	read(fd, buf, sizeof(buf));
	close(fd);

	if (sscanf(buf, "%lf %lf %lf", &load5, &load10, &load15) == 3) {
		load_info.load5 = load5;
		load_info.load10 = load10;
		load_info.load15 = load15;
	}
}

static void
next_cpu (void)
{
	static gboolean initialized = FALSE;
	static gfloat u1, n1, s1, i1;
	gfloat u2, n2, s2, i2;
	gfloat u3, n3, s3, i3;
	gdouble total;
	int fd;
	char buf[1024];

	fd = open("/proc/stat", O_RDONLY);
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
	cpu_info.cpuUsage = (100 * (u3 + n3 + s3)) / total;

  finish:
  	close(fd);
	u1 = u2;
	i1 = i2;
	s1 = s2;
	n1 = n2;
}

static void
next_net (void)
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

	if ((fd = open("/proc/net/dev", O_RDONLY)) < 0) {
		g_warning("Failed to open /proc/net/dev");
		g_assert_not_reached();
	}

	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	line = buf;
	for (i = 0; buf[i]; i++) {
		if (buf[i] == ':') {
			buf[i] = ' ';
		} else if (buf[i] == '\n') {
			buf[i] = '\0';
			if (++l > 2) { // ignore first two lines
				if (sscanf(line, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu",
				           iface, &bytesIn, &dummy1, &dummy2, &dummy3, &dummy4,
				           &dummy5, &dummy6, &dummy7, &bytesOut) != 10) {
					g_warning("Skipping invalid line: %s", line);
				} else if (g_strcmp0(iface, "lo") != 0) {
					totalIn += bytesIn;
					totalOut += bytesOut;
				}
				line = NULL;
			}
			line = &buf[++i];
		}
	}

	if (!initialized) {
		initialized = TRUE;
		goto finish;
	}

	net_info.bytesIn = (totalIn - lastTotalIn);
	net_info.bytesOut = (totalOut - lastTotalOut);

  finish:
	close(fd);
	lastTotalOut = totalOut;
	lastTotalIn = totalIn;
}

static void
next_mem (void)
{
	static gboolean initialized = FALSE;

	gdouble memTotal = 0;
	gdouble memFree = 0;
	gdouble swapTotal = 0;
	gdouble swapFree = 0;
	gdouble cached = 0;
	int fd;
	char buf[4096];
	char *line;
	int i;


	if ((fd = open("/proc/meminfo", O_RDONLY)) < 0) {
		g_warning("Failed to open /proc/meminfo");
		return;
	}

	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	line = buf;

	for (i = 0; buf[i]; i++) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			if (g_str_has_prefix(line, "MemTotal:")) {
				if (sscanf(line, "MemTotal: %lf", &memTotal) != 1) {
					g_warning("Failed to read MemTotal");
					goto error;
				}
			} else if (g_str_has_prefix(line, "MemFree:")) {
				if (sscanf(line, "MemFree: %lf", &memFree) != 1) {
					g_warning("Failed to read MemFree");
					goto error;
				}
			} else if (g_str_has_prefix(line, "SwapTotal:")) {
				if (sscanf(line, "SwapTotal: %lf", &swapTotal) != 1) {
					g_warning("Failed to read SwapTotal");
					goto error;
				}
			} else if (g_str_has_prefix(line, "SwapFree:")) {
				if (sscanf(line, "SwapFree: %lf", &swapFree) != 1) {
					g_warning("Failed to read SwapFree");
					goto error;
				}
			} else if (g_str_has_prefix(line, "Cached:")) {
				if (sscanf(line, "Cached: %lf", &cached) != 1) {
					g_warning("Failed to read Cached");
					goto error;
				}
			}
			line = &buf[i + 1];
		}
	}

	if (!initialized) {
		initialized = TRUE;
		goto finish;
	}

	mem_info.memFree = (memTotal - cached - memFree) / memTotal;
	mem_info.swapFree = (swapTotal - swapFree) / swapTotal;

  finish:
  error:
  	close(fd);
}

static void
next_pmem (void)
{
	static char *path = NULL;
	int fd;
	char buf[1024];
	long size = 0;
	long resident = 0;

	if (G_UNLIKELY(!path)) {
		path = g_strdup_printf("/proc/%d/statm", pid);
	}

	fd = open(path, O_RDONLY);
	read(fd, buf, sizeof(buf));
	sscanf(buf, "%ld %ld", &size, &resident);
	pmem_info.size = size;
	pmem_info.resident = resident;
	close(fd);
}

static void
next_sched (void)
{
	static char *path = NULL;
	static gdouble last_vruntime = 0;
	gdouble vruntime = 0;
	int fd;
	char buf[4096];
	char name[128];
	char *line;
	gint i;

	if (G_UNLIKELY(!path)) {
		path = g_strdup_printf("/proc/%d/sched", pid);
	}

	fd = open(path, O_RDONLY);
	memset(buf, 0, sizeof(buf));
	read(fd, buf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	line = buf;
	for (i = 0; buf[i]; i++) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			if (g_str_has_prefix(line, "se.vruntime")) {
				if (sscanf(line, "%s : %lf", name, &vruntime) != 2) {
					g_printerr("Failed to parse vruntime.\n");
					break;
				}
				sched_info.vruntime = (vruntime - last_vruntime);
				break;
			}
			line = &buf[++i];
		}
	}
	close(fd);
	last_vruntime = vruntime;
}

static void
next_threads (void)
{
	static gchar *path = NULL;
	gint n_threads = 0;
	GDir *dir;

	if (G_UNLIKELY(!path)) {
		path = g_strdup_printf("/proc/%d/task", pid);
	}

	if (!(dir = g_dir_open(path, 0, NULL))) {
		return;
	}
	while (g_dir_read_name(dir)) {
		n_threads++;
	}
	g_dir_close(dir);
	thread_info.n_threads = n_threads;
}

static GtkWidget*
create_main_window (void)
{
	GtkWidget *window;
	GtkWidget *load_label;
	GtkWidget *cpu_label;
	GtkWidget *net_label;
	GtkWidget *mem_label;
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

	cpu_graph = create_graph();
	uber_graph_set_format(UBER_GRAPH(cpu_graph), UBER_GRAPH_PERCENT);
	uber_graph_set_yautoscale(UBER_GRAPH(cpu_graph), FALSE);
	uber_graph_set_yrange(UBER_GRAPH(cpu_graph), &cpu_range);
	uber_graph_add_line(UBER_GRAPH(cpu_graph));
	uber_graph_set_value_func(UBER_GRAPH(cpu_graph), get_cpu, NULL, NULL);

	load_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(load_label), "<b>Load History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), load_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(load_label), .0, .5);
	gtk_widget_show(load_label);

	load_graph = create_graph();
	uber_graph_set_yautoscale(UBER_GRAPH(load_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_add_line(UBER_GRAPH(load_graph));
	uber_graph_set_value_func(UBER_GRAPH(load_graph), get_load, NULL, NULL);

	net_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(net_label), "<b>Network History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), net_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(net_label), .0, .5);
	gtk_widget_show(net_label);

	net_graph = create_graph();
	uber_graph_set_format(UBER_GRAPH(net_graph), UBER_GRAPH_DIRECT1024);
	uber_graph_set_yautoscale(UBER_GRAPH(net_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(net_graph));
	uber_graph_add_line(UBER_GRAPH(net_graph));
	uber_graph_set_value_func(UBER_GRAPH(net_graph), get_net, NULL, NULL);

	mem_label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(mem_label), "<b>Memory History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), mem_label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(mem_label), .0, .5);
	gtk_widget_show(mem_label);

	mem_graph = create_graph();
	uber_graph_set_format(UBER_GRAPH(mem_graph), UBER_GRAPH_PERCENT);
	uber_graph_set_yautoscale(UBER_GRAPH(mem_graph), FALSE);
	uber_graph_add_line(UBER_GRAPH(mem_graph));
	uber_graph_add_line(UBER_GRAPH(mem_graph));
	uber_graph_set_value_func(UBER_GRAPH(mem_graph), get_mem, NULL, NULL);

	next_load();
	next_cpu();
	next_mem();
	next_net();
	next_pmem();
	next_sched();
	next_threads();

	next_load();
	next_cpu();
	next_mem();
	next_net();
	next_pmem();
	next_sched();

	return window;
}

static gboolean
get_pmem (UberGraph *graph,
          gint       line,
          gdouble   *value,
          gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = pmem_info.size;
		break;
	case 2:
		*value = pmem_info.resident;
		break;
	default:
		*value = 0;
		return FALSE;
	}
	return TRUE;
}

static gboolean
get_sched (UberGraph *graph,
           gint       line,
           gdouble   *value,
           gpointer   user_data)
{
	switch (line) {
	case 1:
		*value = sched_info.vruntime;
		break;
	default:
		*value = 0;
		return FALSE;
	}
	return TRUE;
}

static void
create_pid_graphs (GPid pid)
{
	GtkWidget *label;

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "<b>Process Memory History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), .0, .5);
	gtk_widget_show(label);

	pmem_graph = create_graph();
	uber_graph_set_yautoscale(UBER_GRAPH(pmem_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(pmem_graph));
	uber_graph_add_line(UBER_GRAPH(pmem_graph));
	uber_graph_set_value_func(UBER_GRAPH(pmem_graph), get_pmem, NULL, NULL);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "<b>Scheduler Time History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), .0, .5);
	gtk_widget_show(label);

	sched_graph = create_graph();
	uber_graph_set_yautoscale(UBER_GRAPH(sched_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(sched_graph));
	uber_graph_set_value_func(UBER_GRAPH(sched_graph), get_sched, NULL, NULL);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "<b>Thread Count History</b>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), .0, .5);
	gtk_widget_show(label);

	thread_graph = create_graph();
	uber_graph_set_yautoscale(UBER_GRAPH(thread_graph), TRUE);
	uber_graph_add_line(UBER_GRAPH(thread_graph));
	uber_graph_set_value_func(UBER_GRAPH(thread_graph), get_threads, NULL, NULL);
}

static volatile gboolean quit = FALSE;

static gpointer
sample_func (gpointer data)
{
	while (!quit) {
		DEBUG("Running samplers ...");
		next_load();
		next_cpu();
		next_net();
		next_mem();
		next_pmem();
		next_sched();
		next_threads();
		g_usleep(G_USEC_PER_SEC);
	}
	return NULL;
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

static void
child_exited (GPid     pid,
              gint     status,
              gpointer data)
{
	g_printerr("Child exited.\n");
	reaped = TRUE;
	gtk_main_quit();
}

gint
main (gint   argc,
      gchar *argv[])
{
	GError *error = NULL;
	GtkWidget *window;
	gchar **args;
	gint i;

	g_set_application_name(_("uber-graph"));
	g_thread_init(NULL);
	gtk_init(&argc, &argv);

#if 1
	/* run the UberBuffer tests */
	run_buffer_tests();
#endif

	/* initialize sources to -INFINITY */
	cpu_info.cpuUsage = -INFINITY;
	net_info.bytesIn = -INFINITY;
	net_info.bytesOut = -INFINITY;
	mem_info.memFree = -INFINITY;
	mem_info.swapFree = -INFINITY;
	load_info.load5 = -INFINITY;
	load_info.load10 = -INFINITY;
	load_info.load15 = -INFINITY;

	/* if we need to spawn a process, do so */
	if (argc > 1) {
		g_print("Spawning subprocess ...\n");
		args = g_new0(gchar*, argc);
		for (i = 0; i < argc - 1; i++) {
			args[i] = g_strdup(argv[i + 1]);
		}
		if (!g_spawn_async(".", args, NULL,
		                   G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN | G_SPAWN_DO_NOT_REAP_CHILD,
		                   NULL, NULL,
		                   &pid, &error)) {
			g_printerr("%s\n", error->message);
			g_clear_error(&error);
			return EXIT_FAILURE;
		}
		g_child_watch_add(pid, child_exited, NULL);
		g_print("Process %d started.\n", (gint)pid);
	}

	/* run the test gui */
	window = create_main_window();

	/* add application specific graphs */
	if (pid) {
		create_pid_graphs(pid);
	}

	g_signal_connect(window, "delete-event", gtk_main_quit, NULL);
	g_thread_create(sample_func, NULL, FALSE, NULL);

	gtk_main();

	/* kill child process if needed */
	if (!reaped) {
		g_print("Exiting, killing child prcess.\n");
		kill(pid, SIGINT);
	}

	return EXIT_SUCCESS;
}
