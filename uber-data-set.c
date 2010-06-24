/* uber-data-set.c
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

#include "uber-data-set.h"

/**
 * SECTION:uber-data-set.h
 * @title: UberDataSet
 * @short_description: 
 *
 * Section overview.
 */

G_DEFINE_TYPE(UberDataSet, uber_data_set, G_TYPE_OBJECT)

struct _UberDataSetPrivate
{
	gpointer dummy;
};

#if 0
enum
{
	ROW_ADDED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
#endif

/**
 * uber_data_set_new:
 *
 * Creates a new instance of #UberDataSet.
 *
 * Returns: the newly created instance of #UberDataSet.
 * Side effects: None.
 */
UberDataSet*
uber_data_set_new (void)
{
	UberDataSet *data_set;

	data_set = g_object_new(UBER_TYPE_DATA_SET, NULL);
	return data_set;
}

/**
 * uber_data_set_finalize:
 * @object: A #UberDataSet.
 *
 * Finalizer for a #UberDataSet instance.  Frees any resources held by
 * the instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_data_set_finalize (GObject *object) /* IN */
{
	G_OBJECT_CLASS(uber_data_set_parent_class)->finalize(object);
}

/**
 * uber_data_set_class_init:
 * @klass: A #UberDataSetClass.
 *
 * Initializes the #UberDataSetClass and prepares the vtable.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_data_set_class_init (UberDataSetClass *klass) /* IN */
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = uber_data_set_finalize;
	g_type_class_add_private(object_class, sizeof(UberDataSetPrivate));
}

/**
 * uber_data_set_init:
 * @data_set: A #UberDataSet.
 *
 * Initializes the newly created #UberDataSet instance.
 *
 * Returns: None.
 * Side effects: None.
 */
static void
uber_data_set_init (UberDataSet *data_set) /* IN */
{
	data_set->priv = G_TYPE_INSTANCE_GET_PRIVATE(data_set,
	                                             UBER_TYPE_DATA_SET,
	                                             UberDataSetPrivate);
}
