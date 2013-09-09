/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-plugin-loader.h"
#include "gs-plugin-loader-sync.h"

static void
gs_app_func (void)
{
	GsApp *app;

	app = gs_app_new ("gnome-software");
	g_assert (GS_IS_APP (app));

	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-software");

	g_object_unref (app);
}

static guint _status_changed_cnt = 0;

static void
gs_plugin_loader_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    gpointer user_data)
{
	_status_changed_cnt++;
}

static void
gs_plugin_loader_dedupe_func (void)
{
	GsApp *app1;
	GsApp *app2;
	GsPluginLoader *loader;

	loader = gs_plugin_loader_new ();

	/* add app */
	app1 = gs_app_new ("app1");
	gs_app_set_description (app1, "description");
	app1 = gs_plugin_loader_dedupe (loader, app1);
	g_assert_cmpstr (gs_app_get_id (app1), ==, "app1");
	g_assert_cmpstr (gs_app_get_description (app1), ==, "description");

	app2 = gs_app_new ("app1");
	app2 = gs_plugin_loader_dedupe (loader, app2);
	g_assert_cmpstr (gs_app_get_id (app2), ==, "app1");
	g_assert_cmpstr (gs_app_get_description (app2), ==, "description");

	g_object_unref (app1);
	g_object_unref (app2);

	g_object_unref (loader);
}

static void
gs_plugin_loader_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GList *list;
	GList *l;
	GsApp *app;
	GsPluginLoader *loader;

	loader = gs_plugin_loader_new ();
	g_assert (GS_IS_PLUGIN_LOADER (loader));
	g_signal_connect (loader, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb), NULL);

	/* load the plugins */
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* enable some that will give us predictable results */
	ret = gs_plugin_loader_set_enabled (loader, "dummy", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-kind", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-popular", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-ratings", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-filename", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-apps", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "notgoingtoexist", TRUE);
	g_assert (!ret);

	list = gs_plugin_loader_get_popular (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (_status_changed_cnt, ==, 1);
	g_assert_cmpint (g_list_length (list), ==, 6);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-boxes");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Boxes");

	app = g_list_nth_data (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gedit");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Edit text files");

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* get updates */
	_status_changed_cnt = 0;
	list = gs_plugin_loader_get_updates (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (_status_changed_cnt, >=, 1);
	g_assert_cmpint (g_list_length (list), ==, 2);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "os-update:gnome-boxes-libs;0.0.1;i386;updates-testing,libvirt-glib-devel;0.0.1;noarch;fedora");
	g_assert_cmpstr (gs_app_get_name (app), ==, "OS Updates");
//	g_assert_cmpstr (gs_app_get_summary (app), ==, "Includes performance, stability and security improvements for all users\nDo not segfault when using newer versons of libvirt.\nFix several memory leaks.");
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_OS_UPDATE);

	app = g_list_nth_data (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-boxes");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Boxes");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Do not segfault when using newer versons of libvirt.");
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_NORMAL);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* test packagekit */
	gs_plugin_loader_set_enabled (loader, "dummy", FALSE);
	ret = gs_plugin_loader_set_enabled (loader, "packagekit", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "desktopdb", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-apps", TRUE);
	g_assert (ret);

	list = gs_plugin_loader_get_installed (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (g_list_length (list), >, 50);

	/* find a specific app */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (g_strcmp0 (gs_app_get_id (app), "gnome-screenshot") == 0)
			break;
	}
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-screenshot");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Screenshot");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Save images of your screen or individual windows");
	g_assert_cmpint (gs_app_get_state (app), ==, GS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_SYSTEM);
	g_assert (gs_app_get_pixbuf (app) != NULL);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* do this again, which should be much faster */
	list = gs_plugin_loader_get_installed (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (g_list_length (list), >, 50);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* set a rating */
	gs_plugin_loader_set_enabled (loader, "packagekit", FALSE);
	gs_plugin_loader_set_enabled (loader, "desktopdb", FALSE);
	gs_plugin_loader_set_enabled (loader, "datadir-apps", FALSE);
	ret = gs_plugin_loader_set_enabled (loader, "local-ratings", TRUE);
	g_assert (ret);

	/* create a dummy value */
	app = gs_app_new ("self-test");
	gs_app_set_rating (app, 35);
	ret = gs_plugin_loader_app_set_rating (loader, app, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get the saved value */
	gs_app_set_rating (app, -1);
	ret = gs_plugin_loader_app_refine (loader, app, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_rating (app), ==, 35);
	g_object_unref (app);

	g_object_unref (loader);
}

static void
gs_plugin_loader_empty_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GList *apps;
	GList *l;
	GList *l2;
	GList *list;
	GList *subcats;
	GsCategory *category;
	GsCategory *sub;
	GsPluginLoader *loader;
	guint empty_subcats_cnt = 0;

	/* load the plugins */
	loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-menu-spec", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "appstream", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "self-test", TRUE);
	g_assert (ret);

	/* get the list of categories */
	list = gs_plugin_loader_get_categories (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* find how many packages each sub category has */
	for (l = list; l != NULL; l = l->next) {
		category = GS_CATEGORY (l->data);
		subcats = gs_category_get_subcategories (category);
		if (subcats == NULL)
			continue;
		for (l2 = subcats; l2 != NULL; l2 = l2->next) {
			sub = GS_CATEGORY (l2->data);

			/* ignore general */
			if (gs_category_get_id (sub) == NULL)
				continue;

			/* find subcaegories that have no applications */
			apps = gs_plugin_loader_get_category_apps (loader,
								   sub,
								   NULL,
								   &error);
			if (apps == NULL) {
				g_debug ("NOAPPS:\t%s/%s: %s",
					 gs_category_get_id (category),
					 gs_category_get_id (sub),
					 error->message);
				g_clear_error (&error);
				empty_subcats_cnt++;
				//g_warning ("MOO");
			} else {
				GList *g;
				if (g_getenv ("DUMPGROUPS") != NULL) {
					for (g = apps; g != NULL; g = g->next) {
						g_print ("Cat: %s\tSubCat: %s\tPkgName: %s\tAppId: %s\n",
							 gs_category_get_id (category),
							 gs_category_get_id (sub),
							 gs_app_get_metadata_item (GS_APP (g->data), "package-name"),
							 gs_app_get_id (GS_APP (g->data)));
					}
				}
				g_debug ("APPS[%i]:\t%s/%s",
					 g_list_length (apps),
					 gs_category_get_id (category),
					 gs_category_get_id (sub));
			}
			g_list_free_full (apps, (GDestroyNotify) g_object_unref);
		}
	}
	g_assert_cmpint (empty_subcats_cnt, ==, 0);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
	g_object_unref (loader);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/gnome-software/app", gs_app_func);
	if (g_getenv ("HAS_APPSTREAM") != NULL)
		g_test_add_func ("/gnome-software/plugin-loader{empty}", gs_plugin_loader_empty_func);
	g_test_add_func ("/gnome-software/plugin-loader{dedupe}", gs_plugin_loader_dedupe_func);
	if(0)g_test_add_func ("/gnome-software/plugin-loader", gs_plugin_loader_func);

	return g_test_run ();
}

