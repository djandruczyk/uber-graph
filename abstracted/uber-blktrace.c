/* uber-blktrace.c
 *
 * Copyright (C) 2010 Andy Isaacson
 *               2010 Christian Hergert <chris@dronelabs.com>
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

#include <glib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <linux/blktrace_api.h>

#include "uber-blktrace.h"

typedef struct
{
	volatile GAsyncQueue* q;
} IoLatInfo;

struct io_list
{
	struct blk_io_trace *t;
	struct io_list      *next;
};

static struct io_list *iolist = NULL;
static int	           blktrace_fd = -1;
static GPid	           blktrace_pid = 0;
static IoLatInfo       iolat_info = { 0 };

static void
blktrace_exited (GPid     pid,    /* IN */
                 gint     status, /* IN */
                 gpointer data)   /* IN */
{
	g_printerr("blktrace exited.\n");
	blktrace_fd = -1;
}

static int
io_list_len (void)
{
	int i;
	struct io_list *p;

	for (i = 0, p = iolist; p; i++, p = p->next)
		;
	return i;
}

static void G_GNUC_PRINTF(1, 2) G_GNUC_NORETURN
die (const char *fmt, /* IN */
     ...)             /* IN */
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void
setup_blktrace (void)
{
	static const gchar *argv[] = {
		"sudo",
		"/usr/sbin/blktrace",
		"-o-",
		"/dev/sda",
		NULL
	};
	gchar **args;
	GError *error = NULL;
	gint i;
	gint flags;

	args = g_new0(gchar*, G_N_ELEMENTS(argv));
	for (i = 0; i < G_N_ELEMENTS(argv); i++) {
		args[i] = g_strdup(argv[i]);
	}

	if (!g_spawn_async_with_pipes(NULL, args, NULL,
				      G_SPAWN_SEARCH_PATH,
				      NULL, NULL, &blktrace_pid,
				      NULL, &blktrace_fd, NULL,
				      &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		return;
	}
	g_child_watch_add(blktrace_pid, blktrace_exited, NULL);
	if ((flags = fcntl(blktrace_fd, F_GETFL, 0)) == -1)
		die("F_GETFL: %s\n", strerror(errno));
	flags |= O_NONBLOCK;
	if (fcntl(blktrace_fd, F_SETFL, flags) == -1)
		die("F_SETFL: %s\n", strerror(errno));
	g_print("blktrace set up on fd %d\n", blktrace_fd);
}

static void
setup_iolats (void)
{
	setup_blktrace();
	iolat_info.q = g_async_queue_new_full(NULL);
}

static struct blk_io_trace*
find_io (struct blk_io_trace t) /* IN */
{
	struct io_list *p, **prevp = &iolist;
	struct blk_io_trace *r;

	for (p = iolist; p; p = p->next) {
		if (p->t->sector == t.sector) {
			*prevp = p->next;
			r = p->t;
			g_free(p);
			return r;
		}
		prevp = &p->next;
	}
	return NULL;
}

static void
stash_io (struct blk_io_trace t) /* IN */
{
	struct blk_io_trace *p = g_new(struct blk_io_trace, 1);
	struct io_list *n = g_new(struct io_list, 1);

	*p = t;
	n->t = p;
	n->next = iolist;
	iolist = n;
}

static inline int
tvdiff (const GTimeVal a, /* IN */
        const GTimeVal b) /* IN */
{
	return (b.tv_sec - a.tv_sec) * 1000000 + (b.tv_usec - a.tv_usec);
}

void
hexdump (FILE *f, /* IN */
         void *p, /* IN */
         int   n) /* IN */
{
	int i;
	unsigned char *x = p;

	for (i=0; i<n; i++) {
		fprintf(f, "%02x%s", x[i],
		        (i%16 == 15 || i==n-1) ? "\n":i%8==7?"  ":" ");
	}
}

static int
buffered_read (int   fd,   /* IN */
               char *dest, /* IN */
               int   sz)   /* IN */
{
#define BUFSZ 1024
	static int last_fd, head, tail;
	static char buf[BUFSZ];
	char *p = dest;
	int a, n = sz;
	int nbuf;
	int ret = -1;

	/* if we have stuff buffered for another caller, bail. */
	if (fd != last_fd && head != tail)
		return read(fd, dest, sz);

	nbuf = head - tail;

	/* if we have buffered data, copy as much as will fit */
	if (nbuf > 0) {
		a = MIN(n, nbuf);
		memcpy(p, buf + tail, a);
		p += a;
		tail += a;
		n -= a;
		ret = a;
	}

	/* if we didn't satisfy the reader, get another buffer */
	if (n > 0) {
		g_assert(head == tail);

		if (n >= BUFSZ) {
			return read(fd, p, n);
		}
		if ((a = read(fd, buf, BUFSZ)) == -1) {
			goto out;
		}
		if (a == 0) {
			/* EOF!  If we copied out buffered data, out: will
			 * return its len; otherwise we need to return 0.
			 */
			ret = 0;
			goto out;
		}
		last_fd = fd;
		head = a;
		tail = MIN(n, a);
		memcpy(p, buf, tail);
		p += tail;
	}
out:
	/*
	 * if we gave the user any data, then return that length; otherwise,
	 * return error (or EOF).
	 */
	return (p - dest) ?: ret;
}

static int
read_blktrace (int fd,                 /* IN */
               struct blk_io_trace *t) /* IN */
{
	static struct blk_io_trace b;
	static unsigned int n;
	static void *buf;
	static int buflen;
	static int numblk;
	void *p;
	int c;

	p = (char *)&b + n;
	c = buffered_read(fd, p, sizeof(b) - n);

	if (c == -1) {
		if (errno != EAGAIN)
			fprintf(stderr, "read(%d): %s\n", fd, strerror(errno));
		return 0;
	}
	n += c;
	if (n < sizeof(b))
		return 0;

	numblk++;
	if (b.magic != (BLK_IO_TRACE_MAGIC | BLK_IO_TRACE_VERSION)) {
		fprintf(stderr, "wrong magic! record %d buffer =\n", numblk);
		hexdump(stderr, &b, sizeof(b));
		exit(1);
	}

	if (b.pdu_len > 0) {
		if (b.pdu_len > buflen) {
			buflen = b.pdu_len;
			p = realloc(buf, buflen);
			if (!p)
				die("unable to allocate %d bytes\n", buflen);
			buf = p;
		}
		// XXX this should escape back out to the poller
		for (p = buf, c = b.pdu_len; c > 0; ) {
			int a = buffered_read(fd, p, c);

			if (a < 0) continue;
			c -= a;
			p += a;
		}
	}
	*t = b;
	n = 0;
	return 1;
}

void
uber_blktrace_init (void)
{
	setup_iolats();
}

void
uber_blktrace_next (void)
{
	struct blk_io_trace t, *p;
	int i, n = 0, x, td;
	GArray *vals;
	GTimeVal tv1, tv2;

	if (blktrace_fd == -1) {
		return;
	}

	g_get_current_time(&tv1);
	vals = g_array_new(FALSE, FALSE, sizeof(gint));
	g_array_set_size(vals, 0);

	while (read_blktrace(blktrace_fd, &t)) {
		n++;
#if 0
		printf("%-4d 0x%08x %5d 0x%08x %lld %5d %d@%d\n",
				n, (unsigned int)t.magic, (int)t.sequence,
				(unsigned int)t.action,
				(long long)t.time, (int)t.pid,
				(int)t.bytes, (int)t.sector);
#endif
		switch (t.action & 0xffff) {
		case __BLK_TA_COMPLETE:
			p = find_io(t);
			if (!p) {
				fprintf(stderr, "seq %d not found!\n", t.sequence);
				break;
			}
			x = t.time - p->time;
			g_array_append_val(vals, x);
			g_free(p);
			break;
		case __BLK_TA_ISSUE:
			stash_io(t);
			break;
		case __BLK_TA_QUEUE:
		case __BLK_TA_BACKMERGE:
		case __BLK_TA_FRONTMERGE:
		case __BLK_TA_GETRQ:
		case __BLK_TA_SLEEPRQ:
		case __BLK_TA_REQUEUE:
		case __BLK_TA_PLUG:
		case __BLK_TA_UNPLUG_IO:
		case __BLK_TA_UNPLUG_TIMER:
		case __BLK_TA_INSERT:
		case __BLK_TA_SPLIT:
		case __BLK_TA_BOUNCE:
		case __BLK_TA_REMAP:
		case __BLK_TA_ABORT:
		case __BLK_TA_DRV_DATA:
		default:
			break;
		}
	}
	g_get_current_time(&tv2);
	td = tvdiff(tv1, tv2);
	g_print("%s %d records %d us %.2f us/record, %d completions, %d outstanding ",
			G_STRFUNC, n, td, td * 1. / (n?:1), (int)vals->len, io_list_len());
	for (i=0; i<vals->len; i++)
		printf("%d ", (int)g_array_index(vals, gint, i) / 1000);
	printf("\n");
	g_async_queue_push((GAsyncQueue*)iolat_info.q, vals);
}

gboolean
uber_blktrace_get (UberHeatMap  *map,       /* IN */
                   GArray      **values,    /* IN */
                   gpointer      user_data) /* IN */
{
	GArray *v;
	GArray *sum;
	gdouble val;
	gint i;

	sum = g_array_sized_new(FALSE, TRUE, sizeof(gdouble), 1 /* map->nbucket */);
	while ((v = g_async_queue_try_pop((GAsyncQueue *)iolat_info.q)) != NULL) {
		if (v->len == 0) {
			continue;
		}
		for (i = 0; i < v->len; i++) {
			val = (gdouble)g_array_index(v, gint, i) / 1000.;
			g_array_append_val(sum, val);
		}
		g_array_unref(v);
	}
	*values = sum;
	return TRUE;
}

void
uber_blktrace_shutdown (void)
{
	if (blktrace_fd > 0) {
		kill(blktrace_fd, SIGINT);
	}
}
