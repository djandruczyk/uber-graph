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

#include <ctype.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include "uber.h"
#include "uber-blktrace.h"

typedef struct
{
	guint       len;
	gdouble    *total;
	gdouble    *freq;
	glong      *last_user;
	glong      *last_idle;
	glong      *last_system;
	glong      *last_nice;
	GtkWidget **labels;
} CpuInfo;

typedef struct
{
	gdouble total_in;
	gdouble total_out;
	gdouble last_total_in;
	gdouble last_total_out;
} NetInfo;

typedef struct
{
	gulong gdk_event_count;
	gulong x_event_count;
} UIInfo;

static gboolean     want_blktrace    = FALSE;
static UIInfo       ui_info          = { 0 };
static CpuInfo      cpu_info         = { 0 };
static NetInfo      net_info         = { 0 };
static const gchar *default_colors[] = { "#73d216",
                                         "#f57900",
                                         "#3465a4",
                                         "#ef2929",
                                         "#75507b",
                                         "#ce5c00",
                                         "#c17d11",
                                         "#ce5c00",
                                         NULL };

static void
gdk_event_hook (GdkEvent *event, /* IN */
                gpointer  data)  /* IN */
{
	ui_info.gdk_event_count++;
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
		*value = ui_info.gdk_event_count;
		ui_info.gdk_event_count = 0;
		break;
	case 2:
		*value = ui_info.x_event_count;
		ui_info.x_event_count = 0;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

static gboolean
get_cpu_info (UberLineGraph *graph,     /* IN */
              guint          line,      /* IN */
              gdouble       *value,     /* OUT */
              gpointer       user_data) /* IN */
{
	gchar *text;
	gint i;

	g_assert_cmpint(line, >, 0);
	g_assert_cmpint(line, <=, cpu_info.len * 2);

	if ((line % 2) == 0) {
		*value = cpu_info.freq[((line - 1) / 2)];
	} else {
		i = (line - 1) / 2;
		*value = cpu_info.total[i];
		/*
		 * Update label text.
		 */
		text = g_strdup_printf("CPU%d  %0.1f %%", i + 1, *value);
		uber_label_set_text(UBER_LABEL(cpu_info.labels[i]), text);
		g_free(text);
	}
	return TRUE;
}

static gboolean
get_net_info (UberLineGraph *graph,     /* IN */
              guint          line,      /* IN */
              gdouble       *value,     /* IN */
              gpointer       user_data) /* IN */
{
	switch (line) {
	case 1:
		*value = net_info.total_in;
		break;
	case 2:
		*value = net_info.total_out;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

int
XNextEvent (Display *display,      /* IN */
            XEvent  *event_return) /* OUT */
{
	static gsize initialized = FALSE;
	static int (*Real_XNextEvent) (Display*disp, XEvent*evt);
	gpointer lib;
	int ret;
	
	if (G_UNLIKELY(g_once_init_enter(&initialized))) {
		if (!(lib = dlopen("libX11.so.6", RTLD_LAZY))) {
			g_error("Could not load libX11.so.6");
		}
		if (!(Real_XNextEvent = dlsym(lib, "XNextEvent"))) {
			g_error("Could not find XNextEvent in libX11.so.6");
		}
		g_once_init_leave(&initialized, TRUE);
	}

	ret = Real_XNextEvent(display, event_return);
	ui_info.x_event_count++;
	return ret;
}

static void
next_cpu_info (void)
{
	gchar cpu[64] = { 0 };
	glong user;
	glong system;
	glong nice;
	glong idle;
	glong user_calc;
	glong system_calc;
	glong nice_calc;
	glong idle_calc;
	gchar *buf = NULL;
	glong total;
	gchar *line;
	gint ret;
	gint id;
	gint i;

	if (G_UNLIKELY(!cpu_info.len)) {
#if __linux__
		cpu_info.len = get_nprocs();
#else
#error "Your platform is not supported"
#endif
		g_assert(cpu_info.len);
		/*
		 * Allocate data for sampling.
		 */
		cpu_info.total = g_new0(gdouble, cpu_info.len);
		cpu_info.last_user = g_new0(glong, cpu_info.len);
		cpu_info.last_idle = g_new0(glong, cpu_info.len);
		cpu_info.last_system = g_new0(glong, cpu_info.len);
		cpu_info.last_nice = g_new0(glong, cpu_info.len);
		cpu_info.labels = g_new0(GtkWidget*, cpu_info.len);
	}

	if (G_LIKELY(g_file_get_contents("/proc/stat", &buf, NULL, NULL))) {
		line = buf;
		for (i = 0; buf[i]; i++) {
			if (buf[i] == '\n') {
				buf[i] = '\0';
				if (g_str_has_prefix(line, "cpu")) {
					if (isdigit(line[3])) {
						user = nice = system = idle = id = 0;
						ret = sscanf(line, "%s %ld %ld %ld %ld",
						             cpu, &user, &nice, &system, &idle);
						if (ret != 5) {
							goto next;
						}
						ret = sscanf(cpu, "cpu%d", &id);
						if (ret != 1 || id < 0 || id >= cpu_info.len) {
							goto next;
						}
						user_calc = user - cpu_info.last_user[id];
						nice_calc = nice - cpu_info.last_nice[id];
						system_calc = system - cpu_info.last_system[id];
						idle_calc = idle - cpu_info.last_idle[id];
						total = user_calc + nice_calc + system_calc + idle_calc;
						cpu_info.total[id] = (user_calc + nice_calc + system_calc) / (gfloat)total * 100.;
						cpu_info.last_user[id] = user;
						cpu_info.last_nice[id] = nice;
						cpu_info.last_idle[id] = idle;
						cpu_info.last_system[id] = system;
					}
				} else {
					/* CPU info comes first. Skip further lines. */
					break;
				}
			  next:
				line = &buf[i + 1];
			}
		}
	}

	g_free(buf);
}

static void
next_cpu_freq_info (void)
{
	glong max;
	glong cur;
	gboolean ret;
	gchar *buf;
	gchar *path;
	gint i;

	g_return_if_fail(cpu_info.len > 0);

	/*
	 * Initialize.
	 */
	if (!cpu_info.freq) {
		cpu_info.freq = g_new0(gdouble, cpu_info.len);
	}

	/*
	 * Get current frequencies.
	 */
	for (i = 0; i < cpu_info.len; i++) {
		/*
		 * Get max frequency.
		 */
		path = g_strdup_printf("/sys/devices/system/cpu/cpu%d"
		                       "/cpufreq/scaling_max_freq", i);
		ret = g_file_get_contents(path, &buf, NULL, NULL);
		g_free(path);
		if (!ret) {
			continue;
		}
		max = atoi(buf);
		g_free(buf);

		/*
		 * Get current frequency.
		 */
		path = g_strdup_printf("/sys/devices/system/cpu/cpu%d/"
		                       "cpufreq/scaling_cur_freq", i);
		ret = g_file_get_contents(path, &buf, NULL, NULL);
		g_free(path);
		if (!ret) {
			continue;
		}
		cur = atoi(buf);
		g_free(buf);

		/*
		 * Store frequency percentage.
		 */
		cpu_info.freq[i] = (gfloat)cur / (gfloat)max * 100.;
	}
}

static void
next_net_info (void)
{
	GError *error = NULL;
	gulong total_in = 0;
	gulong total_out = 0;
	gulong bytes_in;
	gulong bytes_out;
	gulong dummy;
	gchar *buf = NULL;
	gchar iface[32] = { 0 };
	gchar *line;
	gsize len;
	gint l = 0;
	gint i;

	if (!g_file_get_contents("/proc/net/dev", &buf, &len, &error)) {
		g_printerr("%s", error->message);
		g_error_free(error);
		return;
	}

	line = buf;
	for (i = 0; i < len; i++) {
		if (buf[i] == ':') {
			buf[i] = ' ';
		} else if (buf[i] == '\n') {
			buf[i] = '\0';
			if (++l > 2) { // ignore first two lines
				if (sscanf(line, "%31s %lu %lu %lu %lu %lu %lu %lu %lu %lu",
				           iface, &bytes_in,
				           &dummy, &dummy, &dummy, &dummy,
				           &dummy, &dummy, &dummy,
				           &bytes_out) != 10) {
					g_warning("Skipping invalid line: %s", line);
				} else if (g_strcmp0(iface, "lo") != 0) {
					total_in += bytes_in;
					total_out += bytes_out;
				}
				line = NULL;
			}
			line = &buf[++i];
		}
	}

	if ((net_info.last_total_in != 0.) && (net_info.last_total_out != 0.)) {
		net_info.total_in = (total_in - net_info.last_total_in);
		net_info.total_out = (total_out - net_info.last_total_out);
	}

	net_info.last_total_in = total_in;
	net_info.last_total_out = total_out;
	g_free(buf);
}

static void G_GNUC_NORETURN
sample_thread (gpointer data)
{
	while (TRUE) {
		g_usleep(G_USEC_PER_SEC);
		next_cpu_info();
		next_cpu_freq_info();
		next_net_info();
		if (want_blktrace) {
			uber_blktrace_next();
		}
	}
}

static gboolean
has_freq_scaling (gint cpu)
{
	gboolean ret;
	gchar *path;

	path = g_strdup_printf("/sys/devices/system/cpu/cpu%d/cpufreq", cpu);
	ret = g_file_test(path, G_FILE_TEST_IS_DIR);
	g_free(path);
	return ret;
}

#if 0
static gboolean
dummy_scatter_func (UberScatter  *scatter,   /* IN */
                    GArray      **array,     /* OUT */
                    gpointer      user_data) /* IN */
{
	gdouble val;
	gint i;

	*array = g_array_new(FALSE, FALSE, sizeof(gdouble));
	for (i = 0; i < 4; i++) {
		val = g_random_double_range(0., 100.);
		g_array_append_val(*array, val);
	}
	return TRUE;
}
#endif

gint
main (gint   argc,   /* IN */
      gchar *argv[]) /* IN */
{
	gdouble dashes[] = { 1.0, 4.0 };
	UberRange cpu_range = { 0., 100., 100. };
	UberRange net_range = { 0., 512., 512. };
	UberRange ui_range = { 0., 10., 10. };
	GtkWidget *window;
	GtkWidget *cpu;
	GtkWidget *net;
	GtkWidget *line;
	GtkWidget *map;
	GtkWidget *scatter;
	GtkWidget *label;
	GtkAccelGroup *ag;
	GdkColor color;
	gint lineno;
	gint nprocs;
	gint i;
	gint mod;

	g_thread_init(NULL);
	gtk_init(&argc, &argv);
	nprocs = get_nprocs();
	/*
	 * Check for blktrace hack.
	 */
	if (argc > 1 && (g_strcmp0(argv[1], "--i-can-haz-blktrace") == 0)) {
		want_blktrace = TRUE;
	}
	/*
	 * Warm up differential samplers.
	 */
	next_cpu_info();
	next_cpu_freq_info();
	if (want_blktrace) {
		uber_blktrace_init();
	}
	/*
	 * Install event hook to track how many X events we are doing.
	 */
	gdk_event_handler_set(gdk_event_hook, NULL, NULL);
	/*
	 * Create window and graphs.
	 */
	window = uber_window_new();
	cpu = uber_line_graph_new();
	net = uber_line_graph_new();
	line = uber_line_graph_new();
	map = uber_heat_map_new();
	scatter = uber_scatter_new();
	/*
	 * Configure CPU graph.
	 */
	uber_line_graph_set_autoscale(UBER_LINE_GRAPH(cpu), FALSE);
	uber_graph_set_format(UBER_GRAPH(cpu), UBER_GRAPH_FORMAT_PERCENT);
	uber_line_graph_set_range(UBER_LINE_GRAPH(cpu), &cpu_range);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(cpu),
	                              get_cpu_info, NULL, NULL);
	for (i = 0; i < nprocs; i++) {
		mod = i % G_N_ELEMENTS(default_colors);
		gdk_color_parse(default_colors[mod], &color);
		label = uber_label_new();
		uber_label_set_color(UBER_LABEL(label), &color);
		uber_line_graph_add_line(UBER_LINE_GRAPH(cpu), &color,
		                         UBER_LABEL(label));
		cpu_info.labels[i] = label;
		/*
		 * XXX: Add the line regardless. Just dont populate it if we don't
		 *      have data.
		 */
		lineno = uber_line_graph_add_line(UBER_LINE_GRAPH(cpu), &color, NULL);
		if (has_freq_scaling(i)) {
			uber_line_graph_set_line_dash(UBER_LINE_GRAPH(cpu), lineno,
			                              dashes, G_N_ELEMENTS(dashes), 0);
			uber_line_graph_set_line_alpha(UBER_LINE_GRAPH(cpu), lineno, 1.);
		}
	}
	/*
	 * Add lines for GDK/X events.
	 */
	uber_line_graph_set_range(UBER_LINE_GRAPH(line), &ui_range);
	label = uber_label_new();
	uber_label_set_text(UBER_LABEL(label), "GDK Events");
	gdk_color_parse("#729fcf", &color);
	uber_line_graph_add_line(UBER_LINE_GRAPH(line), &color, UBER_LABEL(label));
	label = uber_label_new();
	uber_label_set_text(UBER_LABEL(label), "X Events");
	gdk_color_parse("#a40000", &color);
	uber_line_graph_add_line(UBER_LINE_GRAPH(line), &color, UBER_LABEL(label));
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(line),
	                              get_xevent_info, NULL, NULL);
	/*
	 * Add lines for bytes in/out.
	 */
	uber_line_graph_set_range(UBER_LINE_GRAPH(net), &net_range);
	uber_line_graph_set_data_func(UBER_LINE_GRAPH(net),
	                              get_net_info, NULL, NULL);
	uber_graph_set_format(UBER_GRAPH(net), UBER_GRAPH_FORMAT_DIRECT1024);
	label = uber_label_new();
	uber_label_set_text(UBER_LABEL(label), "Bytes In");
	gdk_color_parse("#a40000", &color);
	uber_line_graph_add_line(UBER_LINE_GRAPH(net), &color, UBER_LABEL(label));
	label = uber_label_new();
	uber_label_set_text(UBER_LABEL(label), "Bytes Out");
	gdk_color_parse("#4e9a06", &color);
	uber_line_graph_add_line(UBER_LINE_GRAPH(net), &color, UBER_LABEL(label));
	/*
	 * Configure heat map.
	 */
	uber_graph_set_show_ylines(UBER_GRAPH(map), FALSE);
	gdk_color_parse(default_colors[0], &color);
	uber_heat_map_set_fg_color(UBER_HEAT_MAP(map), &color);
#if 0
	uber_heat_map_set_data_func(UBER_HEAT_MAP(map),
	                            uber_blktrace_get,
	                            NULL);
#endif
	/*
	 * Configure scatter.
	 */
	if (want_blktrace) {
		uber_graph_set_show_ylines(UBER_GRAPH(scatter), FALSE);
		gdk_color_parse(default_colors[3], &color);
		uber_scatter_set_fg_color(UBER_SCATTER(scatter), &color);
		uber_scatter_set_data_func(UBER_SCATTER(scatter),
#if 0
								   //dummy_scatter_func, NULL, NULL);
#endif
								   (UberScatterFunc)uber_blktrace_get, NULL, NULL);
		uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(scatter), "IOPS By Size");
		uber_graph_set_show_xlabels(UBER_GRAPH(scatter), TRUE);
		gtk_widget_show(scatter);

		uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(map), "IO Latency");
		uber_graph_set_show_xlabels(UBER_GRAPH(map), FALSE);
		gtk_widget_show(map);
	}
	/*
	 * Add graphs.
	 */
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(cpu), "CPU");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(net), "Network");
	uber_window_add_graph(UBER_WINDOW(window), UBER_GRAPH(line), "UI Events");
	/*
	 * Disable X tick labels by default (except last).
	 */
	uber_graph_set_show_xlabels(UBER_GRAPH(cpu), FALSE);
	uber_graph_set_show_xlabels(UBER_GRAPH(net), FALSE);
	uber_graph_set_show_xlabels(UBER_GRAPH(line), FALSE);
	/*
	 * Show widgets.
	 */
	gtk_widget_show(net);
	gtk_widget_show(line);
	gtk_widget_show(cpu);
	gtk_widget_show(window);
	/*
	 * Show cpu labels by default.
	 */
	uber_window_show_labels(UBER_WINDOW(window), UBER_GRAPH(cpu));
	/*
	 * Setup accelerators.
	 */
	ag = gtk_accel_group_new();
	gtk_accel_group_connect(ag, GDK_w, GDK_CONTROL_MASK, GTK_ACCEL_MASK,
	                        g_cclosure_new(gtk_main_quit, NULL, NULL));
	gtk_window_add_accel_group(GTK_WINDOW(window), ag);
	/*
	 * Attach signals.
	 */
	g_signal_connect(window,
	                 "delete-event",
	                 G_CALLBACK(gtk_main_quit),
	                 NULL);
	/*
	 * Start sampling thread.
	 */
	g_thread_create((GThreadFunc)sample_thread, NULL, FALSE, NULL);
	gtk_main();
	/*
	 * Cleanup after blktrace.
	 */
	if (want_blktrace) {
		uber_blktrace_shutdown();
	}
	return EXIT_SUCCESS;
}
