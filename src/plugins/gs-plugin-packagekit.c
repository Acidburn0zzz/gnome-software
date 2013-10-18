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

#include <config.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>
#include <glib/gi18n.h>

#include <gs-plugin.h>

#include "packagekit-common.h"

struct GsPluginPrivate {
	PkTask			*task;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->task = pk_task_new ();
	pk_client_set_background (PK_CLIENT (plugin->priv->task), FALSE);
	pk_client_set_interactive (PK_CLIENT (plugin->priv->task), FALSE);
	pk_client_set_cache_age (PK_CLIENT (plugin->priv->task), G_MAXUINT);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 10.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->task);
}

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	GsPluginStatus plugin_status;
	PkStatusEnum status;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);
	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_packagekit_add_installed_results:
 */
static gboolean
gs_plugin_packagekit_add_installed_results (GsPlugin *plugin,
					    GList **list,
					    PkResults *results,
					    GError **error)
{
	const gchar *package_id;
	gboolean ret = TRUE;
	GHashTable *installed = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_filtered = NULL;
	GsApp *app;
	guint i;
	PkError *error_code = NULL;
	PkPackage *package;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to get-packages: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}

	/* add all installed packages to a hash */
	installed = g_hash_table_new (g_str_hash, g_str_equal);
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		if (pk_package_get_info (package) != PK_INFO_ENUM_INSTALLED)
			continue;
		g_hash_table_insert (installed,
				     (const gpointer) pk_package_get_name (package),
				     (const gpointer) pk_package_get_id (package));
	}

	/* if the search returns more than one package with the same name,
	 * ignore everything with that name except the installed package */
	array_filtered = g_ptr_array_new ();
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_id = g_hash_table_lookup (installed, pk_package_get_name (package));
		if (pk_package_get_info (package) == PK_INFO_ENUM_INSTALLED || package_id == NULL) {
			g_ptr_array_add (array_filtered, package);
		} else {
			g_debug ("ignoring available %s as installed %s also reported",
				 pk_package_get_id (package), package_id);
		}
	}

	/* process packages */
	for (i = 0; i < array_filtered->len; i++) {
		package = g_ptr_array_index (array_filtered, i);

		app = gs_app_new (NULL);
		gs_app_set_metadata (app,
				     "PackageKit::package-id",
				     pk_package_get_id (package));
		gs_app_set_metadata (app,
				     "PackageKit::package-summary",
				     pk_package_get_summary (package));
		gs_app_set_source (app, pk_package_get_name (package));
		gs_app_set_management_plugin (app, "PackageKit");
		gs_app_set_version (app, pk_package_get_version (package));
		switch (pk_package_get_info (package)) {
		case PK_INFO_ENUM_INSTALLED:
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
			break;
		case PK_INFO_ENUM_AVAILABLE:
			gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
			break;
		default:
			gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
			g_warning ("unknown info state of %s",
				   pk_info_enum_to_string (pk_package_get_info (package)));
		}
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
		g_object_unref (app);
	}
out:
	if (installed != NULL)
		g_hash_table_unref (installed);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (array_filtered != NULL)
		g_ptr_array_unref (array_filtered);
	return ret;
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      const gchar *value,
		      GList **list,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *values[2] = { NULL, NULL };
	gboolean ret = TRUE;
	PkBitfield filter;
	PkResults *results;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	values[0] = value;
	results = pk_client_search_details (PK_CLIENT(plugin->priv->task),
					    filter,
					    (gchar **) values,
					    cancellable,
					    gs_plugin_packagekit_progress_cb, plugin,
					    error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	ret = gs_plugin_packagekit_add_installed_results (plugin,
							  list,
							  results,
							  error);
	if (!ret)
		goto out;
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	gboolean ret = TRUE;
	PkBitfield filter;
	PkResults *results;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	results = pk_client_get_packages (PK_CLIENT(plugin->priv->task),
					  filter,
					  cancellable,
					  gs_plugin_packagekit_progress_cb, plugin,
					  error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	ret = gs_plugin_packagekit_add_installed_results (plugin,
							  list,
							  results,
							  error);
	if (!ret)
		goto out;
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_app_install:
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *package_id;
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	PkResults *results = NULL;

	package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
	if (package_id == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "installing not supported");
		goto out;
	}
	to_array[0] = package_id;
	results = pk_task_install_packages_sync (plugin->priv->task,
						 (gchar **) to_array,
						 cancellable,
						 gs_plugin_packagekit_progress_cb, plugin,
						 error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to install package: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_app_remove:
 */
gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *package_id;
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	PkResults *results = NULL;

	package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
	if (package_id == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "removing not supported");
		goto out;
	}
	to_array[0] = package_id;
	results = pk_task_remove_packages_sync (plugin->priv->task,
						(gchar **) to_array,
						TRUE, FALSE,
						cancellable,
						gs_plugin_packagekit_progress_cb, plugin,
						error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to remove package: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

#if 0
/**
 * gs_plugin_add_categories:
 */
gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GList **list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsCategory *category;

	/* TRANSLATORS: these are extra things that add to existing programs,
	 * for instance fonts, codecs or language packs */
	category = gs_category_new (NULL, "PK::add-ons", _("Add-ons"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PK::codecs",
								/* TRANSLATORS: audio/video decoders */
								_("Codecs")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PK::fonts",
								/* TRANSLATORS: fonts of different styles or languages */
								_("Fonts")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PK::inputs",
								/* TRANSLATORS: these are ways to enter text */
								_("Input Sources")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PK::languages",
								/* TRANSLATORS: additional languages for apps */
								_("Language Packs")));
	*list = g_list_prepend (*list, category);

	return TRUE;
}
#endif
