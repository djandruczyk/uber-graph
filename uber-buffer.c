/* uber-buffer.c
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

#include <string.h>

#include "uber-buffer.h"

#define DEFAULT_SIZE (64)

/**
 * uber_buffer_dispose:
 * @buffer: A #UberBuffer.
 *
 * Cleans up the #UberBuffer instance and frees any allocated resources.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_buffer_dispose (UberBuffer *buffer) /* IN */
{
	g_free(buffer->buffer);
}

/**
 * uber_buffer_new:
 *
 * Creates a new instance of #UberBuffer.
 *
 * Returns: the newly created instance which should be freed with
 *   uber_buffer_unref().
 * Side effects: None.
 */
UberBuffer*
uber_buffer_new (void)
{
	UberBuffer *buffer;

	buffer = g_slice_new0(UberBuffer);
	buffer->ref_count = 1;
	buffer->buffer = g_new0(gdouble, DEFAULT_SIZE);
	buffer->len = DEFAULT_SIZE;
	buffer->pos = 0;
	return buffer;
}

/**
 * uber_buffer_set_size:
 * @buffer: A #UberBuffer.
 *
 * Resizes the circular buffer.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_buffer_set_size (UberBuffer *buffer, /* IN */
                      gint        size)   /* IN */
{
	gint count;

	g_return_if_fail(buffer != NULL);
	g_return_if_fail(size > 0);

	if (size == buffer->len) {
		return;
	}
	g_debug("%s():%d", G_STRFUNC, __LINE__);
	if (size > buffer->len) {
		g_debug("%s():%d", G_STRFUNC, __LINE__);
		buffer->buffer = g_realloc_n(buffer->buffer, size, sizeof(gdouble));
		memset(&buffer->buffer[buffer->len], 0,
		       (size - buffer->len) * sizeof(gdouble));
		if ((count = buffer->len - buffer->pos)) {
			g_debug("%s():%d", G_STRFUNC, __LINE__);
			memmove(&buffer->buffer[size - count],
			        &buffer->buffer[buffer->pos],
			        count * sizeof(gdouble));
			if (size - count > buffer->pos) {
				g_debug("%s():%d", G_STRFUNC, __LINE__);
				memset(&buffer->buffer[buffer->pos], 0,
				       (size - count - buffer->pos) * sizeof(gdouble));
			}
		}
		buffer->len = size;
		return;
	}
	if (size >= buffer->pos) {
		g_debug("%s():%d", G_STRFUNC, __LINE__);
		memmove(&buffer->buffer[buffer->pos],
		        &buffer->buffer[size],
		        (buffer->len - size) * sizeof(gdouble));
		buffer->buffer = g_realloc_n(buffer->buffer, size, sizeof(gdouble));
		buffer->len = size;
		return;
	}
	g_debug("%s():%d", G_STRFUNC, __LINE__);
	memmove(buffer->buffer, &buffer->buffer[buffer->pos - size],
	        size * sizeof(gdouble));
	buffer->buffer = g_realloc_n(buffer->buffer, size, sizeof(gdouble));
	buffer->pos = 0;
	buffer->len = size;
}

/**
 * uber_buffer_foreach:
 * @buffer: A #UberBuffer.
 *
 * Iterates through each item in the circular buffer from the current
 * position to the oldest value.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_buffer_foreach (UberBuffer        *buffer,    /* IN */
                     UberBufferForeach  func,      /* IN */
                     gpointer           user_data) /* IN */
{
	gint i;

	g_return_if_fail(buffer != NULL);
	g_return_if_fail(func != NULL);

		/*
		 * Iterate through data starting from current position working our
		 * way towards the beginning of the buffer.
		 */
	for (i = buffer->pos - 1; i >= 0; i--) {
		g_debug("Loop0, %d", i);
		if (func(buffer, buffer->buffer[i], user_data)) {
			return;
		}
	}

	/*
	 * Iterate from the end of the buffer working our way back towards the
	 * beginning.
	 */
	for (i = buffer->len - 1; i >= buffer->pos; i--) {
		g_debug("Loop1, %d", i);
		if (func(buffer, buffer->buffer[i], user_data)) {
			return;
		}
	}
}

/**
 * uber_buffer_append:
 * @buffer: A #UberBuffer.
 * @value: A #gdouble.
 *
 * Appends a new value onto the circular buffer.
 *
 * Returns: None.
 * Side effects: None.
 */
void
uber_buffer_append (UberBuffer *buffer, /* IN */
                    gdouble     value)  /* IN */
{
	g_return_if_fail(buffer != NULL);

	buffer->buffer[buffer->pos++] = value;
	if (buffer->pos >= buffer->len) {
		buffer->pos = 0;
	}
}

/**
 * UberBuffer_ref:
 * @buffer: A #UberBuffer.
 *
 * Atomically increments the reference count of @buffer by one.
 *
 * Returns: A reference to @buffer.
 * Side effects: None.
 */
UberBuffer*
uber_buffer_ref (UberBuffer *buffer) /* IN */
{
	g_return_val_if_fail(buffer != NULL, NULL);
	g_return_val_if_fail(buffer->ref_count > 0, NULL);

	g_atomic_int_inc(&buffer->ref_count);
	return buffer;
}

/**
 * uber_buffer_unref:
 * @buffer: A UberBuffer.
 *
 * Atomically decrements the reference count of @buffer by one.  When the
 * reference count reaches zero, the structure will be destroyed and
 * freed.
 *
 * Returns: None.
 * Side effects: The structure will be freed when the reference count
 *   reaches zero.
 */
void
uber_buffer_unref (UberBuffer *buffer) /* IN */
{
	g_return_if_fail(buffer != NULL);
	g_return_if_fail(buffer->ref_count > 0);

	if (g_atomic_int_dec_and_test(&buffer->ref_count)) {
		uber_buffer_dispose(buffer);
		g_slice_free(UberBuffer, buffer);
	}
}
