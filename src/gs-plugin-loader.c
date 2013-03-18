/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2013 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>

#include "gs-plugin-loader.h"
#include "gs-plugin.h"

static void	gs_plugin_loader_finalize	(GObject	*object);

#define GS_PLUGIN_LOADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_PLUGIN_LOADER, GsPluginLoaderPrivate))

struct GsPluginLoaderPrivate
{
	GPtrArray		*plugins;
	gchar			*location;
	GsPluginStatus		 status_last;
};

G_DEFINE_TYPE (GsPluginLoader, gs_plugin_loader, G_TYPE_OBJECT)

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_plugin_loader_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
gs_plugin_loader_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gs_plugin_loader_error");
	return quark;
}

/**
 * gs_plugin_loader_run_refine:
 **/
static gboolean
gs_plugin_loader_run_refine (GsPluginLoader *plugin_loader,
			     GList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret = TRUE;
	GsPlugin *plugin;
	const gchar *function_name = "gs_plugin_refine";
	GsPluginRefineFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		g_debug ("run %s on %s", function_name,
			 g_module_name (plugin->module));
		g_timer_start (plugin->timer);
		ret = plugin_func (plugin, list, cancellable, error);
		if (!ret)
			goto out;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * gs_plugin_loader_run_results:
 **/
static GList *
gs_plugin_loader_run_results (GsPluginLoader *plugin_loader,
			      const gchar *function_name,
			      GCancellable *cancellable,
			      GError **error)
{
	gboolean ret = TRUE;
	GList *list = NULL;
	GsPlugin *plugin;
	GsPluginResultsFunc plugin_func = NULL;
	guint i;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (function_name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, error);
		if (ret) {
			ret = FALSE;
			goto out;
		}
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		g_debug ("run %s on %s", function_name,
			 g_module_name (plugin->module));
		g_timer_start (plugin->timer);

		g_assert (error == NULL || *error == NULL);
		ret = plugin_func (plugin, &list, cancellable, error);
		if (!ret)
			goto out;

		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   list,
					   cancellable,
					   error);
	if (!ret)
		goto out;

	/* no results */
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no updates to show");
		goto out;
	}
out:
	if (!ret) {
		g_list_free_full (list, (GDestroyNotify) g_object_unref);
		list = NULL;
	}
	return list;
}

/**
 * gs_plugin_loader_app_is_valid:
 **/
static gboolean
gs_plugin_loader_app_is_valid (GsApp *app)
{
	/* don't show unconverted packages in the application view */
	if (gs_app_get_kind (app) == GS_APP_KIND_PACKAGE)
		return FALSE;
	/* don't show apps that do not have a name */
	if (gs_app_get_name (app) == NULL)
		return FALSE;
	if (gs_app_get_summary (app) == NULL)
		return FALSE;
	return TRUE;
}

/**
 * gs_plugin_loader_remove_invalid:
 **/
static GList *
gs_plugin_loader_remove_invalid (GList *list)
{
	GList *l;
	GsApp *app;

	for (l = list; l != NULL;) {
		app = GS_APP (l->data);
		if (gs_plugin_loader_app_is_valid (app)) {
			l = l->next;
			continue;
		}
		g_debug ("removing package %s", gs_app_get_id (app));
		g_object_unref (app);
		l = list = g_list_delete_link (list, l);
	}
	return list;
}

/******************************************************************************/

/* async state */
typedef struct {
	gboolean			 ret;
	GCancellable			*cancellable;
	GList				*list;
	GSimpleAsyncResult		*res;
	GsPluginLoader			*plugin_loader;
} GsPluginLoaderAsyncState;

/******************************************************************************/

/**
 * cd_plugin_loader_get_all_state_finish:
 **/
static void
cd_plugin_loader_get_all_state_finish (GsPluginLoaderAsyncState *state,
				       const GError *error)
{
	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_list_copy (state->list),
							   (GDestroyNotify) g_list_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);

	g_list_free (state->list);
	g_object_unref (state->res);
	g_object_unref (state->plugin_loader);
	g_slice_free (GsPluginLoaderAsyncState, state);
}

/******************************************************************************/

/**
 * cd_plugin_loader_get_updates_thread_cb:
 **/
static void
cd_plugin_loader_get_updates_thread_cb (GSimpleAsyncResult *res,
					GObject *object,
					GCancellable *cancellable)
{
	const gchar *tmp;
	gboolean has_os_update = FALSE;
	GdkPixbuf *pixbuf = NULL;
	GError *error = NULL;
	GList *l;
	GsApp *app;
	GsApp *app_tmp;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GString *str_id = NULL;
	GString *str_summary = NULL;

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_updates",
						    cancellable,
						    &error);
	if (state->list == NULL) {
		cd_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* coalesce all packages down into one os-update */
	for (l = state->list; l != NULL; l = l->next) {
		app_tmp = GS_APP (l->data);
		if (gs_app_get_kind (app_tmp) == GS_APP_KIND_PACKAGE) {
			has_os_update = TRUE;
		} else {
			/* if we have update text, then use it */
			tmp = gs_app_get_metadata_item (app_tmp, "update-details");
			if (tmp != NULL && tmp[0] != '\0')
				gs_app_set_summary (app_tmp, tmp);
		}
	}

	/* smush them all together */
	if (has_os_update) {
		str_summary = g_string_new (_("Includes performance, stability and security improvements for all users"));
		g_string_append (str_summary, "\n\n\n");
		str_id = g_string_new ("os-update:");
		for (l = state->list; l != NULL; l = l->next) {
			app_tmp = GS_APP (l->data);
			if (gs_app_get_kind (app_tmp) != GS_APP_KIND_PACKAGE)
				continue;
			g_string_append_printf (str_id, "%s,",
						gs_app_get_id (app_tmp));
			g_string_append_printf (str_summary, "%s:\n\n%s\n\n",
						gs_app_get_metadata_item (app_tmp, "update-name"),
						gs_app_get_metadata_item (app_tmp, "update-details"));
		}
		g_string_truncate (str_id, str_id->len - 1);
		g_string_truncate (str_summary, str_summary->len - 1);

		/* load icon */
		pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						   "software-update-available-symbolic",
						   64,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   &error);
		if (pixbuf == NULL) {
			g_warning ("Failed to find software-update-available-symbolic: %s",
				   error->message);
			g_error_free (error);
		}

		/* create new meta object */
		app = gs_app_new (str_id->str);
		gs_app_set_kind (app, GS_APP_KIND_OS_UPDATE);
		gs_app_set_name (app, _("OS Updates"));
		gs_app_set_summary (app, str_summary->str);
		gs_app_set_version (app, "3.6.3");
		gs_app_set_pixbuf (app, pixbuf);
		gs_plugin_add_app (&state->list, app);

		/* remove any packages that are not proper applications or
		 * OS updates */
		state->list = gs_plugin_loader_remove_invalid (state->list);
		if (state->list == NULL) {
			g_set_error_literal (&error,
					     GS_PLUGIN_LOADER_ERROR,
					     GS_PLUGIN_LOADER_ERROR_FAILED,
					     "no updates to show after invalid");
			cd_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
	}

	/* success */
	state->ret = TRUE;
	cd_plugin_loader_get_all_state_finish (state, NULL);
out:
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	if (str_id != NULL)
		g_string_free (str_id, TRUE);
	if (str_summary != NULL)
		g_string_free (str_summary, TRUE);
}

/**
 * gs_plugin_loader_get_updates_async:
 **/
void
gs_plugin_loader_get_updates_async (GsPluginLoader *plugin_loader,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GCancellable *tmp;
	GsPluginLoaderAsyncState *state;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->res = g_simple_async_result_new (G_OBJECT (plugin_loader),
						callback,
						user_data,
						gs_plugin_loader_get_updates_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     cd_plugin_loader_get_updates_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_updates_finish:
 **/
GList *
gs_plugin_loader_get_updates_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* failed */
	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	/* grab detail */
	return g_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * cd_plugin_loader_get_installed_thread_cb:
 **/
static void
cd_plugin_loader_get_installed_thread_cb (GSimpleAsyncResult *res,
					  GObject *object,
					  GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_installed",
						    cancellable,
						    &error);
	if (state->list == NULL) {
		cd_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error_literal (&error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "no installed applications to show after invalid");
		cd_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	cd_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_installed_async:
 **/
void
gs_plugin_loader_get_installed_async (GsPluginLoader *plugin_loader,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GCancellable *tmp;
	GsPluginLoaderAsyncState *state;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->res = g_simple_async_result_new (G_OBJECT (plugin_loader),
						callback,
						user_data,
						gs_plugin_loader_get_installed_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     cd_plugin_loader_get_installed_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_installed_finish:
 **/
GList *
gs_plugin_loader_get_installed_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* failed */
	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	/* grab detail */
	return g_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * cd_plugin_loader_get_popular_thread_cb:
 **/
static void
cd_plugin_loader_get_popular_thread_cb (GSimpleAsyncResult *res,
					  GObject *object,
					  GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_popular",
						    cancellable,
						    &error);
	if (state->list == NULL) {
		cd_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error_literal (&error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "no popular apps to show");
		cd_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	cd_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_popular_async:
 **/
void
gs_plugin_loader_get_popular_async (GsPluginLoader *plugin_loader,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GCancellable *tmp;
	GsPluginLoaderAsyncState *state;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->res = g_simple_async_result_new (G_OBJECT (plugin_loader),
						callback,
						user_data,
						gs_plugin_loader_get_popular_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     cd_plugin_loader_get_popular_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_popular_finish:
 **/
GList *
gs_plugin_loader_get_popular_finish (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* failed */
	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	/* grab detail */
	return g_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_search:
 **/
GList *
gs_plugin_loader_search (GsPluginLoader *plugin_loader,
			 const gchar *value,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *function_name = "gs_plugin_add_search";
	gboolean ret = TRUE;
	GList *list = NULL;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, error);
		if (ret) {
			ret = FALSE;
			goto out;
		}
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		g_debug ("run %s on %s", function_name,
			 g_module_name (plugin->module));
		g_timer_start (plugin->timer);
		ret = plugin_func (plugin, value, &list, cancellable, error);
		if (!ret)
			goto out;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   list,
					   cancellable,
					   error);
	if (!ret)
		goto out;

	/* success */
	list = gs_plugin_loader_remove_invalid (list);
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no search results to show");
		goto out;
	}
out:
	if (!ret) {
		g_list_free_full (list, (GDestroyNotify) g_object_unref);
		list = NULL;
	}
	return list;
}


/**
 * gs_plugin_loader_run_action:
 **/
static gboolean
gs_plugin_loader_run_action (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     const gchar *function_name,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean exists;
	gboolean ret = FALSE;
	gboolean anything_ran = FALSE;
	GError *error_local = NULL;
	GsPluginActionFunc plugin_func = NULL;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, error);
		if (ret) {
			ret = FALSE;
			goto out;
		}
		exists = g_module_symbol (plugin->module,
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		g_debug ("run %s on %s", function_name,
			 g_module_name (plugin->module));
		g_timer_start (plugin->timer);
		ret = plugin_func (plugin, app, cancellable, &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED)) {
				g_debug ("not supported for plugin %s",
					 plugin->name);
				g_clear_error (&error_local);
			} else {
				g_propagate_error (error, error_local);
				goto out;
			}
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
		anything_ran = TRUE;
	}

	/* nothing ran */
	if (!anything_ran) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no plugin could handle %s",
			     function_name);
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * gs_plugin_loader_app_update:
 **/
gboolean
gs_plugin_loader_app_update (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_update",
					    cancellable,
					    error);
}

/**
 * gs_plugin_loader_app_install:
 **/
gboolean
gs_plugin_loader_app_install (GsPluginLoader *plugin_loader,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_install",
					    cancellable,
					    error);
}

/**
 * gs_plugin_loader_app_remove:
 **/
gboolean
gs_plugin_loader_app_remove (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_remove",
					    cancellable,
					    error);
}

/**
 * gs_plugin_loader_app_set_rating:
 **/
gboolean
gs_plugin_loader_app_set_rating (GsPluginLoader *plugin_loader,
				 GsApp *app,
				 GCancellable *cancellable,
				 GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_set_rating",
					    cancellable,
					    error);
}

/**
 * gs_plugin_loader_app_refine:
 *
 * ...really just for make check use.
 **/
gboolean
gs_plugin_loader_app_refine (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret;
	GList *list = NULL;

	gs_plugin_add_app (&list, app);
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   list,
					   cancellable,
					   error);
	if (!ret)
		goto out;
	g_list_free (list);
out:
	return ret;
}

/**
 * gs_plugin_loader_run:
 **/
static void
gs_plugin_loader_run (GsPluginLoader *plugin_loader, const gchar *function_name)
{
	gboolean ret;
	GsPluginFunc plugin_func = NULL;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		g_debug ("run %s on %s", function_name,
			 g_module_name (plugin->module));
		plugin_func (plugin);
	}
}

/**
 * gs_plugin_loader_set_enabled:
 */
gboolean
gs_plugin_loader_set_enabled (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name,
			      gboolean enabled)
{
	gboolean ret = FALSE;
	GsPlugin *plugin;
	guint i;

	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (g_strcmp0 (plugin->name, plugin_name) == 0) {
			plugin->enabled = enabled;
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * gs_plugin_loader_status_update_cb:
 */
static void
gs_plugin_loader_status_update_cb (GsPlugin *plugin,
				   GsApp *app,
				   GsPluginStatus status,
				   gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);

	/* same as last time */
	if (app == NULL && status == plugin_loader->priv->status_last)
		return;

	/* new, or an app, so emit */
	g_debug ("emitting %s(%s)",
		 gs_plugin_status_to_string (status),
		 app != NULL ? gs_app_get_id (app) : "<general>");
	plugin_loader->priv->status_last = status;
	g_signal_emit (plugin_loader,
		       signals[SIGNAL_STATUS_CHANGED],
		       0, app, status);
}

/**
 * gs_plugin_loader_open_plugin:
 */
static GsPlugin *
gs_plugin_loader_open_plugin (GsPluginLoader *plugin_loader,
			      const gchar *filename)
{
	gboolean ret;
	GModule *module;
	GsPluginGetNameFunc plugin_name = NULL;
	GsPluginGetPriorityFunc plugin_prio = NULL;
	GsPlugin *plugin = NULL;

	module = g_module_open (filename, 0);
	if (module == NULL) {
		g_warning ("failed to open plugin %s: %s",
			   filename, g_module_error ());
		goto out;
	}

	/* get description */
	ret = g_module_symbol (module,
			       "gs_plugin_get_name",
			       (gpointer *) &plugin_name);
	if (!ret) {
		g_warning ("Plugin %s requires name", filename);
		g_module_close (module);
		goto out;
	}

	/* get priority */
	ret = g_module_symbol (module,
			       "gs_plugin_get_priority",
			       (gpointer *) &plugin_prio);
	if (!ret) {
		g_warning ("Plugin %s requires priority", filename);
		g_module_close (module);
		goto out;
	}

	/* print what we know */
	plugin = g_slice_new0 (GsPlugin);
	plugin->enabled = FALSE;
	plugin->module = module;
	plugin->pixbuf_size = 64;
	plugin->priority = plugin_prio (plugin);
	plugin->name = g_strdup (plugin_name ());
	plugin->timer = g_timer_new ();
	plugin->status_update_fn = gs_plugin_loader_status_update_cb;
	plugin->status_update_user_data = plugin_loader;
	g_debug ("opened plugin %s: %s", filename, plugin->name);

	/* add to array */
	g_ptr_array_add (plugin_loader->priv->plugins, plugin);
out:
	return plugin;
}

/**
 * gs_plugin_loader_set_location:
 */
void
gs_plugin_loader_set_location (GsPluginLoader *plugin_loader, const gchar *location)
{
	g_free (plugin_loader->priv->location);

	/* something non-default specified */
	if (location != NULL) {
		plugin_loader->priv->location = g_strdup (location);
		return;
	}

	/* use the default, but this requires a 'make install' */
	plugin_loader->priv->location = g_build_filename (LIBDIR, "gs-plugins", NULL);
}

/**
 * gs_plugin_loader_plugin_sort_fn:
 */
static gint
gs_plugin_loader_plugin_sort_fn (gconstpointer a, gconstpointer b)
{
	GsPlugin **pa = (GsPlugin **) a;
	GsPlugin **pb = (GsPlugin **) b;
	return (*pa)->priority < (*pb)->priority;
}

/**
 * gs_plugin_loader_setup:
 */
gboolean
gs_plugin_loader_setup (GsPluginLoader *plugin_loader, GError **error)
{
	const gchar *filename_tmp;
	gboolean ret = TRUE;
	gchar *filename_plugin;
	GDir *dir;
	GsPlugin *plugin;
	guint i;

	g_return_val_if_fail (plugin_loader->priv->location != NULL, FALSE);

	/* search in the plugin directory for plugins */
	dir = g_dir_open (plugin_loader->priv->location, 0, error);
	if (dir == NULL) {
		ret = FALSE;
		goto out;
	}

	/* try to open each plugin */
	g_debug ("searching for plugins in %s", plugin_loader->priv->location);
	do {
		filename_tmp = g_dir_read_name (dir);
		if (filename_tmp == NULL)
			break;
		if (!g_str_has_suffix (filename_tmp, ".so"))
			continue;
		filename_plugin = g_build_filename (plugin_loader->priv->location,
						    filename_tmp,
						    NULL);
		gs_plugin_loader_open_plugin (plugin_loader, filename_plugin);
		g_free (filename_plugin);
	} while (TRUE);

	/* sort by priority */
	g_ptr_array_sort (plugin_loader->priv->plugins,
			  gs_plugin_loader_plugin_sort_fn);

	/* print what the priorities are */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		g_debug ("%.1f\t->\t%s [%s]",
			 plugin->priority,
			 plugin->name,
			 plugin->enabled ? "enabled" : "disabled");
	}

	/* run the plugins */
	gs_plugin_loader_run (plugin_loader, "gs_plugin_initialize");
out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_loader_plugin_free:
 **/
static void
gs_plugin_loader_plugin_free (GsPlugin *plugin)
{
	g_free (plugin->priv);
	g_free (plugin->name);
	g_module_close (plugin->module);
	g_timer_destroy (plugin->timer);
	g_slice_free (GsPlugin, plugin);
}

/**
 * gs_plugin_loader_class_init:
 * @klass: The GsPluginLoaderClass
 **/
static void
gs_plugin_loader_class_init (GsPluginLoaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_plugin_loader_finalize;

	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (GsPluginLoaderPrivate));
}

/**
 * gs_plugin_loader_init:
 **/
static void
gs_plugin_loader_init (GsPluginLoader *plugin_loader)
{
	plugin_loader->priv = GS_PLUGIN_LOADER_GET_PRIVATE (plugin_loader);
	plugin_loader->priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_plugin_loader_plugin_free);
	plugin_loader->priv->status_last = GS_PLUGIN_STATUS_LAST;
}

/**
 * gs_plugin_loader_finalize:
 * @object: The object to finalize
 **/
static void
gs_plugin_loader_finalize (GObject *object)
{
	GsPluginLoader *plugin_loader;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GS_IS_PLUGIN_LOADER (object));

	plugin_loader = GS_PLUGIN_LOADER (object);

	g_return_if_fail (plugin_loader->priv != NULL);

	/* run the plugins */
	gs_plugin_loader_run (plugin_loader, "gs_plugin_destroy");

	g_ptr_array_unref (plugin_loader->priv->plugins);
	g_free (plugin_loader->priv->location);

	G_OBJECT_CLASS (gs_plugin_loader_parent_class)->finalize (object);
}

/**
 * gs_plugin_loader_new:
 *
 * Return value: a new GsPluginLoader object.
 **/
GsPluginLoader *
gs_plugin_loader_new (void)
{
	GsPluginLoader *plugin_loader;
	plugin_loader = g_object_new (GS_TYPE_PLUGIN_LOADER, NULL);
	return GS_PLUGIN_LOADER (plugin_loader);
}
