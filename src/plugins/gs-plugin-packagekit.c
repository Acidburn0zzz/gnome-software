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

#include <gs-plugin.h>

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
	g_object_set (plugin->priv->task,
		      "background", FALSE,
		      NULL);
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
	GsPluginStatus plugin_status = GS_PLUGIN_STATUS_UNKNOWN;
	PkStatusEnum status;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* set label */
	switch (status) {
	case PK_STATUS_ENUM_SETUP:
	case PK_STATUS_ENUM_FINISHED:
	case PK_STATUS_ENUM_UNKNOWN:
		break;
	case PK_STATUS_ENUM_WAIT:
	case PK_STATUS_ENUM_WAITING_FOR_LOCK:
	case PK_STATUS_ENUM_WAITING_FOR_AUTH:
		plugin_status = GS_PLUGIN_STATUS_WAITING;
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
	case PK_STATUS_ENUM_TEST_COMMIT:
		plugin_status = GS_PLUGIN_STATUS_SETUP;
		break;
	case PK_STATUS_ENUM_DOWNLOAD:
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
	case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
	case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
	case PK_STATUS_ENUM_DOWNLOAD_GROUP:
	case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
		plugin_status = GS_PLUGIN_STATUS_DOWNLOADING;
		break;
	case PK_STATUS_ENUM_QUERY:
	case PK_STATUS_ENUM_INFO:
	case PK_STATUS_ENUM_DEP_RESOLVE:
		plugin_status = GS_PLUGIN_STATUS_QUERYING;
		break;
	default:
		g_warning ("no mapping for %s",
			   pk_status_enum_to_string (status));
		break;
	}
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      const gchar *value,
		      GList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	return TRUE;
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
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
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

	/* get data */
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		app = gs_app_new (pk_package_get_id (package));
		gs_app_set_metadata (app,
				     "package-id",
				     pk_package_get_id (package));
		gs_app_set_metadata (app,
				     "package-name",
				     pk_package_get_name (package));
		gs_app_set_metadata (app,
				     "package-summary",
				     pk_package_get_summary (package));
		gs_app_set_version (app, pk_package_get_version (package));
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
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
 * gs_plugin_packagekit_add_updates_results:
 */
static gboolean
gs_plugin_packagekit_add_updates_results (GsPlugin *plugin,
					  GList **list,
					  PkResults *results,
					  GError **error)
{
	gboolean ret = TRUE;
	gchar *package_id;
	gchar **split;
	gchar *update_text;
	GPtrArray *array = NULL;
	GsApp *app;
	guint i;
	PkError *error_code = NULL;
	PkUpdateDetail *update_detail;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to get-update-details: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_update_detail_array (results);
	if (array->len == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no update details were returned");
		goto out;
	}
	for (i = 0; i < array->len; i++) {
		update_detail = g_ptr_array_index (array, i);
		g_object_get (update_detail,
			      "package-id", &package_id,
			      "update-text", &update_text,
			      NULL);
		split = pk_package_id_split (package_id);
		app = gs_app_new (package_id);
		gs_app_set_metadata (app, "package-id", package_id);
		gs_app_set_metadata (app, "package-name", split[PK_PACKAGE_ID_NAME]);
		gs_app_set_metadata (app, "update-name", split[PK_PACKAGE_ID_NAME]);
		gs_app_set_metadata (app, "update-details", update_text);
		gs_app_set_name (app, split[PK_PACKAGE_ID_NAME]);
		gs_app_set_version (app, split[PK_PACKAGE_ID_VERSION]);
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
		g_free (package_id);
		g_free (update_text);
		g_strfreev (split);
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	PkBitfield filter;
	PkPackageSack *sack = NULL;
	PkResults *results = NULL;
	PkResults *results2 = NULL;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_DOWNLOADED,
					 -1);
	results = pk_client_get_updates (PK_CLIENT (plugin->priv->task),
					 filter,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get update details */
	sack = pk_results_get_package_sack (results);
	if (pk_package_sack_get_size (sack) == 0)
		goto out;
	package_ids = pk_package_sack_get_ids (sack);
	results2 = pk_client_get_update_detail (PK_CLIENT (plugin->priv->task),
						package_ids,
						cancellable,
						gs_plugin_packagekit_progress_cb, plugin,
						error);
	if (results2 == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	ret = gs_plugin_packagekit_add_updates_results (plugin,
							list,
							results2,
							error);
	if (!ret)
		goto out;
out:
	g_strfreev (package_ids);
	if (sack != NULL)
		g_object_unref (sack);
	if (results != NULL)
		g_object_unref (results);
	if (results2 != NULL)
		g_object_unref (results2);
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

	package_id = gs_app_get_metadata_item (app, "package-id");
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

	package_id = gs_app_get_metadata_item (app, "package-id");
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
