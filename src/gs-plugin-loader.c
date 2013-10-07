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
#include "gs-profile.h"

static void	gs_plugin_loader_finalize	(GObject	*object);

#define GS_PLUGIN_LOADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_PLUGIN_LOADER, GsPluginLoaderPrivate))

struct GsPluginLoaderPrivate
{
	GPtrArray		*plugins;
	gchar			*location;
	GsPluginStatus		 status_last;
	GsProfile		*profile;

	GMutex			 pending_apps_mutex;
	GPtrArray		*pending_apps;

	GMutex			 app_cache_mutex;
	GHashTable		*app_cache;
	gchar			**compatible_projects;
};

G_DEFINE_TYPE (GsPluginLoader, gs_plugin_loader, G_TYPE_OBJECT)

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PENDING_APPS_CHANGED,
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
 * gs_plugin_loader_app_sort_cb:
 **/
static gint
gs_plugin_loader_app_sort_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (gs_app_get_name (GS_APP (a)),
			  gs_app_get_name (GS_APP (b)));
}

/**
 * gs_plugin_loader_dedupe:
 */
GsApp *
gs_plugin_loader_dedupe (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsApp *new_app;
	GsPluginLoaderPrivate *priv = plugin_loader->priv;

	g_mutex_lock (&plugin_loader->priv->app_cache_mutex);

	/* not yet set */
	if (gs_app_get_id (app) == NULL) {
		new_app = app;
		goto out;
	}

	/* already exists */
	new_app = g_hash_table_lookup (priv->app_cache, gs_app_get_id (app));
	if (new_app != NULL) {
		/* an [updatable] installable package is more information than
		 * just the fact that something is installed */
		if (gs_app_get_state (app) == GS_APP_STATE_UPDATABLE &&
		    gs_app_get_state (new_app) == GS_APP_STATE_INSTALLED) {
			/* we have to do the little dance to appease the
			 * angry gnome controlling the state-machine */
			gs_app_set_state (new_app, GS_APP_STATE_UNKNOWN);
			gs_app_set_state (new_app, GS_APP_STATE_UPDATABLE);
		}

		/* save any properties we already know */
		if (gs_app_get_source (app) != NULL)
			gs_app_set_source (new_app, gs_app_get_source (app));
		if (gs_app_get_project_group (app) != NULL)
			gs_app_set_project_group (new_app, gs_app_get_project_group (app));
		if (gs_app_get_name (app) != NULL)
			gs_app_set_name (new_app, gs_app_get_name (app));
		if (gs_app_get_summary (app) != NULL)
			gs_app_set_summary (new_app, gs_app_get_summary (app));
		if (gs_app_get_description (app) != NULL)
			gs_app_set_description (new_app, gs_app_get_description (app));
		if (gs_app_get_update_details (app) != NULL)
			gs_app_set_update_details (new_app, gs_app_get_update_details (app));
		if (gs_app_get_update_version (app) != NULL)
			gs_app_set_update_version (new_app, gs_app_get_update_version (app));
		if (gs_app_get_pixbuf (app) != NULL)
			gs_app_set_pixbuf (new_app, gs_app_get_pixbuf (app));

		/* this looks a little odd to unref the method parameter,
		 * but it allows us to do:
		 * app = gs_plugin_loader_dedupe (cache, app);
		 */
		g_object_unref (app);
		g_object_ref (new_app);
		goto out;
	}

	/* insert new entry */
	g_hash_table_insert (priv->app_cache,
			     (gpointer) gs_app_get_id (app),
			     g_object_ref (app));

	/* no ref */
	new_app = app;
out:
	g_mutex_unlock (&plugin_loader->priv->app_cache_mutex);
	return new_app;
}

/**
 * gs_plugin_loader_list_uniq:
 **/
static GList *
gs_plugin_loader_list_uniq (GsPluginLoader *plugin_loader, GList *list)
{
	const gchar *id;
	GHashTable *hash;
	GList *l;
	GList *list_new = NULL;
	GsApp *app;
	GsApp *found;

	/* create a new list with just the unique items */
	hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		id = gs_app_get_id (app);
		if (id == NULL) {
			list_new = g_list_prepend (list_new, g_object_ref (app));
			continue;
		}
		found = g_hash_table_lookup (hash, id);
		if (found == NULL) {
			list_new = g_list_prepend (list_new, g_object_ref (app));
			g_hash_table_insert (hash, (gpointer) id, GUINT_TO_POINTER (1));
			continue;
		}
		g_debug ("ignoring duplicate %s", id);
	}

	gs_plugin_list_free (list);
	g_hash_table_unref (hash);
	return list_new;
}

/**
 * gs_plugin_loader_list_dedupe:
 **/
static void
gs_plugin_loader_list_dedupe (GsPluginLoader *plugin_loader, GList *list)
{
	GList *l;
	for (l = list; l != NULL; l = l->next)
		l->data = gs_plugin_loader_dedupe (plugin_loader, GS_APP (l->data));
}

/**
 * gs_plugin_loader_run_refine:
 **/
static gboolean
gs_plugin_loader_run_refine (GsPluginLoader *plugin_loader,
			     const gchar *function_name_parent,
			     GList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret = TRUE;
	gchar *profile_id;
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
		profile_id = g_strdup_printf ("GsPlugin::%s(%s;%s)",
					      plugin->name,
					      function_name_parent,
					      function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		ret = plugin_func (plugin, list, cancellable, error);
		if (!ret)
			goto out;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
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
	gchar *profile_id_parent;
	gchar *profile_id;
	GList *list = NULL;
	GsPlugin *plugin;
	GsPluginResultsFunc plugin_func = NULL;
	guint i;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (function_name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);

	/* profile */
	profile_id_parent = g_strdup_printf ("GsPlugin::*(%s)",
					     function_name);
	gs_profile_start (plugin_loader->priv->profile, profile_id_parent);

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
		profile_id = g_strdup_printf ("GsPlugin::%s(%s)",
					      plugin->name, function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		g_assert (error == NULL || *error == NULL);
		ret = plugin_func (plugin, &list, cancellable, error);
		if (!ret)
			goto out;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   list,
					   cancellable,
					   error);
	if (!ret)
		goto out;

	/* remove duplicates */
	list = gs_plugin_loader_list_uniq (plugin_loader, list);

	/* profile */
	gs_profile_stop (plugin_loader->priv->profile, profile_id_parent);

	/* no results */
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no results to show");
		goto out;
	}
out:
	g_free (profile_id_parent);
	if (!ret) {
		gs_plugin_list_free (list);
		list = NULL;
	}
	return list;
}

/**
 * gs_plugin_loader_get_app_str:
 **/
static const gchar *
gs_plugin_loader_get_app_str (GsApp *app)
{
	const gchar *id;

	/* first try the actual id */
	id = gs_app_get_id (app);
	if (id != NULL)
		return id;

	/* first try the actual id */
	id = gs_app_get_metadata_item (app, "PackageKit::package-id");
	if (id != NULL)
		return id;

	/* urmmm */
	return "<invalid>";
}

/**
 * gs_plugin_loader_app_is_valid:
 **/
static gboolean
gs_plugin_loader_app_is_valid (GsApp *app)
{
	/* don't show unknown state */
	if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) {
		g_debug ("app invalid as state unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown kind */
	if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN) {
		g_debug ("app invalid as kind unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted packages in the application view */
	if (gs_app_get_kind (app) == GS_APP_KIND_PACKAGE) {
		g_debug ("app invalid as only a package %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show apps that do not have the required details */
	if (gs_app_get_source (app) == NULL) {
		g_debug ("app invalid as no source %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	if (gs_app_get_name (app) == NULL) {
		g_debug ("app invalid as no name %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	if (gs_app_get_summary (app) == NULL) {
		g_debug ("app invalid as no summary %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
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
		g_object_unref (app);
		l = list = g_list_delete_link (list, l);
	}
	return list;
}

/**
 * gs_plugin_loader_remove_system:
 **/
static GList *
gs_plugin_loader_remove_system (GList *list)
{
	GList *l;
	GsApp *app;

	for (l = list; l != NULL;) {
		app = GS_APP (l->data);
		if (gs_app_get_kind (app) != GS_APP_KIND_SYSTEM) {
			l = l->next;
			continue;
		}
		g_debug ("removing package %s", gs_app_get_id (app));
		g_object_unref (app);
		l = list = g_list_delete_link (list, l);
	}
	return list;
}


/**
 * gs_plugin_loader_get_app_is_compatible:
 */
static gboolean
gs_plugin_loader_get_app_is_compatible (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = plugin_loader->priv;
	const gchar *tmp;
	guint i;

	/* search for any compatible projects */
	tmp = gs_app_get_project_group (app);
	if (tmp == NULL)
		return TRUE;
	for (i = 0; priv->compatible_projects[i] != NULL; i++) {
		if (g_strcmp0 (tmp,  priv->compatible_projects[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_plugin_loader_remove_incompat:
 **/
static GList *
gs_plugin_loader_remove_incompat (GsPluginLoader *plugin_loader, GList *list)
{
	GList *l;
	GList *new = NULL;
	GsApp *app;
	gboolean compat;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		compat = gs_plugin_loader_get_app_is_compatible (plugin_loader, app);
		if (compat) {
			gs_plugin_add_app (&new, app);
		} else {
			g_debug ("removing incompatible %s from project group %s",
				 gs_app_get_id (app),
				 gs_app_get_project_group (app));
		}
	}
	gs_plugin_list_free (list);
	return new;
}

/******************************************************************************/

/* async state */
typedef struct {
	gboolean			 ret;
	GCancellable			*cancellable;
	GList				*list;
	GSimpleAsyncResult		*res;
	GsPluginLoader			*plugin_loader;
	GsPluginLoaderFlags		 flags;
	gchar				*value;
	GsCategory			*category;
} GsPluginLoaderAsyncState;

/******************************************************************************/

/**
 * gs_plugin_loader_get_all_state_finish:
 **/
static void
gs_plugin_loader_get_all_state_finish (GsPluginLoaderAsyncState *state,
				       const GError *error)
{
	if (state->ret) {
		GList *list;
		list = g_list_copy_deep (state->list, (GCopyFunc) g_object_ref, NULL);
		g_simple_async_result_set_op_res_gpointer (state->res,
							   list,
							   (GDestroyNotify) gs_plugin_list_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);

	g_free (state->value);
	gs_plugin_list_free (state->list);
	g_object_unref (state->res);
	g_object_unref (state->plugin_loader);
	g_slice_free (GsPluginLoaderAsyncState, state);
}

/******************************************************************************/

/**
 * gs_plugin_loader_add_os_update_item:
 **/
static GList *
gs_plugin_loader_add_os_update_item (GList *list)
{
	gboolean has_os_update = FALSE;
	GError *error = NULL;
	GdkPixbuf *pixbuf = NULL;
	GList *l;
	GList *list_new = list;
	GsApp *app_os;
	GsApp *app_tmp;

	/* do we have any packages left that are not apps? */
	for (l = list; l != NULL; l = l->next) {
		app_tmp = GS_APP (l->data);
		if (gs_app_get_kind (app_tmp) == GS_APP_KIND_PACKAGE)
			has_os_update = TRUE;
	}
	if (!has_os_update)
		goto out;

	/* create new meta object */
	app_os = gs_app_new ("os-update");
	gs_app_set_kind (app_os, GS_APP_KIND_OS_UPDATE);
	gs_app_set_state (app_os, GS_APP_STATE_UPDATABLE);
	gs_app_set_source (app_os, "os-update");
	/* TRANSLATORS: this is a group of updates that are not packages and
	 * are ot shown in the main list */
	gs_app_set_name (app_os, _("OS Updates"));
	/* TRANSLATORS: this is a longer description of the os-update item */
	gs_app_set_summary (app_os, _("Includes performance, stability and security improvements for all users."));
	gs_app_set_description (app_os, _("Includes performance, stability and security improvements for all users."));
	for (l = list; l != NULL; l = l->next) {
		app_tmp = GS_APP (l->data);
		if (gs_app_get_kind (app_tmp) != GS_APP_KIND_PACKAGE)
			continue;
		gs_app_add_related (app_os, app_tmp);
	}

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
	} else {
		gs_app_set_pixbuf (app_os, pixbuf);
	}
	list_new = g_list_prepend (list, app_os);
out:
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	return list_new;
}

/**
 * gs_plugin_loader_get_updates_thread_cb:
 **/
static void
gs_plugin_loader_get_updates_thread_cb (GSimpleAsyncResult *res,
					GObject *object,
					GCancellable *cancellable)
{
	const gchar *method_name = "gs_plugin_add_updates";
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);

	/* do things that would block */
	if ((state->flags & GS_PLUGIN_LOADER_FLAGS_USE_HISTORY) > 0)
		method_name = "gs_plugin_add_updates_historical";

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    method_name,
						    cancellable,
						    &error);
	if (state->list == NULL) {
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* remove duplicates */
	state->list = gs_plugin_loader_list_uniq (plugin_loader, state->list);

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* coalesce all packages down into one os-update */
	state->list = gs_plugin_loader_add_os_update_item (state->list);

	/* remove any packages that are not proper applications or
	 * OS updates */
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error_literal (&error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "no updates to show after invalid");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_updates_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_updates()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE that remain after refining are
 * added to a new virtual #GsApp of kind %GS_APP_KIND_OS_UPDATE and all the
 * %GS_APP_KIND_PACKAGE objects are moved to related packages of this object.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL, %GS_APP_KIND_SYSTEM or %GS_APP_KIND_OS_UPDATE.
 *
 * The #GsApps may be in state %GS_APP_STATE_INSTALLED or %GS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_updates_async (GsPluginLoader *plugin_loader,
				    GsPluginLoaderFlags flags,
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
	state->flags = flags;
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_get_updates_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_updates_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_installed_thread_cb:
 **/
static void
gs_plugin_loader_get_installed_thread_cb (GSimpleAsyncResult *res,
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
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->list = gs_plugin_loader_remove_system (state->list);
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error_literal (&error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "no installed applications to show after invalid");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_installed_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_installed()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps will all initially be in state %GS_APP_STATE_INSTALLED.
 **/
void
gs_plugin_loader_get_installed_async (GsPluginLoader *plugin_loader,
				      GsPluginLoaderFlags flags,
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
	state->flags = flags;
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_get_installed_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_installed_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_popular_thread_cb:
 **/
static void
gs_plugin_loader_get_popular_thread_cb (GSimpleAsyncResult *res,
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
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error_literal (&error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "no popular apps to show");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_popular_async:
 **/
void
gs_plugin_loader_get_popular_async (GsPluginLoader *plugin_loader,
				    GsPluginLoaderFlags flags,
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
	state->flags = flags;
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_get_popular_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_popular_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_featured_thread_cb:
 **/
static void
gs_plugin_loader_get_featured_thread_cb (GSimpleAsyncResult *res,
					  GObject *object,
					  GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_featured",
						    cancellable,
						    &error);
	if (state->list == NULL) {
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error_literal (&error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "no featured apps to show");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_featured_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_featured()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE that remain after refining are
 * automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL or %GS_APP_KIND_SYSTEM.
 *
 * The #GsApps may be in state %GS_APP_STATE_INSTALLED or %GS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_featured_async (GsPluginLoader *plugin_loader,
				     GsPluginLoaderFlags flags,
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
						gs_plugin_loader_get_featured_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	state->flags = flags;
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_get_featured_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_featured_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_featured_finish (GsPluginLoader *plugin_loader,
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_search_thread_cb:
 **/
static void
gs_plugin_loader_search_thread_cb (GSimpleAsyncResult *res,
				   GObject *object,
				   GCancellable *cancellable)
{
	const gchar *function_name = "gs_plugin_add_search";
	gboolean ret = TRUE;
	gchar *profile_id;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, &error);
		if (ret) {
			gs_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		profile_id = g_strdup_printf ("GsPlugin::%s(%s)",
					      plugin->name, function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		ret = plugin_func (plugin, state->value, &state->list, cancellable, &error);
		if (!ret) {
			gs_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   cancellable,
					   &error);
	if (!ret) {
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* remove duplicates */
	state->list = gs_plugin_loader_list_uniq (plugin_loader, state->list);

	/* remove incompatible projects */
	state->list = gs_plugin_loader_remove_incompat (plugin_loader, state->list);

	/* success */
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error (&error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no search results to show");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_search_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps may be in state %GS_APP_STATE_INSTALLED or %GS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_async (GsPluginLoader *plugin_loader,
			       const gchar *value,
			       GsPluginLoaderFlags flags,
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
						gs_plugin_loader_search_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	state->flags = flags;
	state->value = g_strdup (value);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_search_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_search_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_search_finish (GsPluginLoader *plugin_loader,
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_category_sort_cb:
 **/
static gint
gs_plugin_loader_category_sort_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (gs_category_get_name (GS_CATEGORY (a)),
			  gs_category_get_name (GS_CATEGORY (b)));
}

/**
 * gs_plugin_loader_get_categories_thread_cb:
 **/
static void
gs_plugin_loader_get_categories_thread_cb (GSimpleAsyncResult *res,
					   GObject *object,
					   GCancellable *cancellable)
{
	const gchar *function_name = "gs_plugin_add_categories";
	gboolean ret = TRUE;
	gchar *profile_id;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPlugin *plugin;
	GsPluginResultsFunc plugin_func = NULL;
	GList *l;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, &error);
		if (ret) {
			gs_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		profile_id = g_strdup_printf ("GsPlugin::%s(%s)",
					      plugin->name, function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		ret = plugin_func (plugin, &state->list, cancellable, &error);
		if (!ret) {
			gs_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
	}

	/* sort by name */
	state->list = g_list_sort (state->list, gs_plugin_loader_category_sort_cb);
	for (l = state->list; l != NULL; l = l->next)
		gs_category_sort_subcategories (GS_CATEGORY (l->data));

	/* success */
	if (state->list == NULL) {
		g_set_error (&error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no categories to show");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* success */
	state->ret = TRUE;
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_categories_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_categories()
 * function. The plugins return #GsCategory objects.
 **/
void
gs_plugin_loader_get_categories_async (GsPluginLoader *plugin_loader,
				       GsPluginLoaderFlags flags,
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
						gs_plugin_loader_get_categories_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	state->flags = flags;
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_get_categories_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_categories_finish:
 *
 * Return value: (element-type GsCategory) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_categories_finish (GsPluginLoader *plugin_loader,
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_category_apps_thread_cb:
 **/
static void
gs_plugin_loader_get_category_apps_thread_cb (GSimpleAsyncResult *res,
					      GObject *object,
					      GCancellable *cancellable)
{
	const gchar *function_name = "gs_plugin_add_category_apps";
	gboolean ret = TRUE;
	gchar *profile_id;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) g_object_get_data (G_OBJECT (cancellable), "state");
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPlugin *plugin;
	GsPluginCategoryFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, &error);
		if (ret) {
			gs_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		profile_id = g_strdup_printf ("GsPlugin::%s(%s)",
					      plugin->name, function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		ret = plugin_func (plugin, state->category, &state->list, cancellable, &error);
		if (!ret) {
			gs_plugin_loader_get_all_state_finish (state, error);
			g_error_free (error);
			goto out;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   cancellable,
					   &error);
	if (!ret) {
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* remove duplicates */
	state->list = gs_plugin_loader_list_uniq (plugin_loader, state->list);

	/* remove incompatible projects */
	state->list = gs_plugin_loader_remove_incompat (plugin_loader, state->list);

	/* success */
	state->list = gs_plugin_loader_remove_system (state->list);
	state->list = gs_plugin_loader_remove_invalid (state->list);
	if (state->list == NULL) {
		g_set_error (&error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no get_category_apps results to show");
		gs_plugin_loader_get_all_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* sort, just in case the UI doesn't do this */
	state->list = g_list_sort (state->list, gs_plugin_loader_app_sort_cb);

	/* success */
	state->ret = TRUE;
	g_object_unref (state->category);
	gs_plugin_loader_get_all_state_finish (state, NULL);
out:
	return;
}

/**
 * gs_plugin_loader_get_category_apps_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_category_apps()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps may be in state %GS_APP_STATE_INSTALLED or %GS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_category_apps_async (GsPluginLoader *plugin_loader,
					  GsCategory *category,
					  GsPluginLoaderFlags flags,
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
						gs_plugin_loader_get_category_apps_async);
	state->plugin_loader = g_object_ref (plugin_loader);
	state->flags = flags;
	state->category = g_object_ref (category);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* run in a thread */
	tmp = g_cancellable_new ();
	g_object_set_data (G_OBJECT (tmp), "state", state);
	g_simple_async_result_run_in_thread (G_SIMPLE_ASYNC_RESULT (state->res),
					     gs_plugin_loader_get_category_apps_thread_cb,
					     0,
					     (GCancellable *) tmp);
	g_object_unref (tmp);
}

/**
 * gs_plugin_loader_get_category_apps_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_category_apps_finish (GsPluginLoader *plugin_loader,
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
	return gs_plugin_list_copy (g_simple_async_result_get_op_res_gpointer (simple));
}

/******************************************************************************/

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
	gchar *profile_id;
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
		profile_id = g_strdup_printf ("GsPlugin::%s(%s)",
					      plugin->name, function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		ret = plugin_func (plugin, app, cancellable, &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED)) {
				g_debug ("not supported for plugin %s: %s",
					 plugin->name,
					 error_local->message);
				g_clear_error (&error_local);
			} else {
				g_propagate_error (error, error_local);
				goto out;
			}
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
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
 * gs_plugin_loader_get_state_for_app:
 **/
GsAppState
gs_plugin_loader_get_state_for_app (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsAppState state = GS_APP_STATE_UNKNOWN;
	GsApp *tmp;
	GsPluginLoaderPrivate *priv = plugin_loader->priv;
	guint i;

	for (i = 0; i < priv->pending_apps->len; i++) {
		tmp = g_ptr_array_index (priv->pending_apps, i);
		if (g_strcmp0 (gs_app_get_id (tmp), gs_app_get_id (app)) == 0) {
			state = gs_app_get_state (tmp);
			break;
		}
	}
	return state;
}

/**
 * gs_plugin_loader_get_pending:
 **/
GPtrArray *
gs_plugin_loader_get_pending (GsPluginLoader *plugin_loader)
{
	return g_ptr_array_ref (plugin_loader->priv->pending_apps);
}

typedef struct {
	GsPluginLoader	*plugin_loader;
	GsApp		*app;
	GCancellable	*cancellable;
	GThread		*thread;
	const gchar	*function_name;
	GsAppState	 state_progress;
	GsAppState	 state_success;
	GsAppState	 state_failure;
	GsPluginLoaderFinishedFunc func;
	gpointer	 func_user_data;
} GsPluginLoaderThreadHelper;

static gboolean
emit_pending_apps_idle (gpointer loader)
{
	g_signal_emit (loader, signals[SIGNAL_PENDING_APPS_CHANGED], 0);
	g_object_unref (loader);

	return G_SOURCE_REMOVE;
}

typedef struct {
	GsApp *app;
	GsAppState state;
} AppStateData;

static gboolean
set_state_idle_cb (gpointer data)
{
	AppStateData *app_data = data;

	gs_app_set_state (app_data->app, app_data->state);
	g_object_unref (app_data->app);
	g_free (app_data);

	return G_SOURCE_REMOVE;
}

static void
gs_app_set_state_in_idle (GsApp *app, GsAppState state)
{
	AppStateData *app_data;

	app_data = g_new (AppStateData, 1);
	app_data->app = g_object_ref (app);
	app_data->state = state;

	g_idle_add (set_state_idle_cb, app_data);
}

/**
 * gs_plugin_loader_thread_func:
 **/
static gpointer
gs_plugin_loader_thread_func (gpointer user_data)
{
	GsPluginLoaderThreadHelper *helper = (GsPluginLoaderThreadHelper *) user_data;
	gboolean ret;
	GError *error = NULL;

	/* add to list */
	gs_app_set_state_in_idle (helper->app, helper->state_progress);
	g_mutex_lock (&helper->plugin_loader->priv->pending_apps_mutex);
	g_ptr_array_add (helper->plugin_loader->priv->pending_apps, g_object_ref (helper->app));
	g_mutex_unlock (&helper->plugin_loader->priv->pending_apps_mutex);
	g_idle_add (emit_pending_apps_idle, g_object_ref (helper->plugin_loader));

	/* run action */
	ret = gs_plugin_loader_run_action (helper->plugin_loader,
					   helper->app,
					   helper->function_name,
					   helper->cancellable,
					   &error);
	if (!ret) {
		gs_app_set_state_in_idle (helper->app, helper->state_failure);
		g_warning ("failed to complete %s: %s", helper->function_name, error->message);
		g_error_free (error);
	} else {
		gs_app_set_state_in_idle (helper->app, helper->state_success);
	}

	/* remove from list */
	g_mutex_lock (&helper->plugin_loader->priv->pending_apps_mutex);
	g_ptr_array_remove (helper->plugin_loader->priv->pending_apps, helper->app);
	g_mutex_unlock (&helper->plugin_loader->priv->pending_apps_mutex);
	g_idle_add (emit_pending_apps_idle, g_object_ref (helper->plugin_loader));

	/* fire finished func */
	if (helper->func != NULL) {
		helper->func (helper->plugin_loader,
			      ret ? helper->app : NULL,
			      helper->func_user_data);
	}

	g_object_unref (helper->plugin_loader);
	g_object_unref (helper->app);
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	g_free (helper);
	return NULL;
}

/**
 * gs_plugin_loader_app_install:
 **/
void
gs_plugin_loader_app_install (GsPluginLoader *plugin_loader,
			      GsApp *app,
			      GsPluginLoaderFlags flags,
			      GCancellable *cancellable,
			      GsPluginLoaderFinishedFunc func,
			      gpointer user_data)
{
	GsPluginLoaderThreadHelper *helper;
	helper = g_new0 (GsPluginLoaderThreadHelper, 1);
	helper->plugin_loader = g_object_ref (plugin_loader);
	helper->app = g_object_ref (app);
	helper->func = func;
	helper->func_user_data = user_data;
	if (cancellable != NULL)
		helper->cancellable = g_object_ref (cancellable);
	helper->function_name = "gs_plugin_app_install";
	helper->state_progress = GS_APP_STATE_INSTALLING;
	helper->state_success = GS_APP_STATE_INSTALLED;
	helper->state_failure = GS_APP_STATE_AVAILABLE;
	helper->thread = g_thread_new ("GsPluginLoader::install",
				       gs_plugin_loader_thread_func,
				       helper);
}

/**
 * gs_plugin_loader_app_remove:
 **/
void
gs_plugin_loader_app_remove (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GsPluginLoaderFlags flags,
			     GCancellable *cancellable,
			     GsPluginLoaderFinishedFunc func,
			     gpointer user_data)
{
	GsPluginLoaderThreadHelper *helper;
	helper = g_new0 (GsPluginLoaderThreadHelper, 1);
	helper->plugin_loader = g_object_ref (plugin_loader);
	helper->app = g_object_ref (app);
	helper->func = func;
	helper->func_user_data = user_data;
	if (cancellable != NULL)
		helper->cancellable = g_object_ref (cancellable);
	helper->function_name = "gs_plugin_app_remove";
	helper->state_progress = GS_APP_STATE_REMOVING;
	helper->state_success = GS_APP_STATE_AVAILABLE;
	helper->state_failure = GS_APP_STATE_INSTALLED;
	helper->thread = g_thread_new ("GsPluginLoader::remove",
				       gs_plugin_loader_thread_func,
				       helper);
}

/**
 * gs_plugin_loader_app_set_rating:
 **/
gboolean
gs_plugin_loader_app_set_rating (GsPluginLoader *plugin_loader,
				 GsApp *app,
				 GsPluginLoaderFlags flags,
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
			     GsPluginLoaderFlags flags,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret;
	GList *list = NULL;

	gs_plugin_add_app (&list, app);
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   NULL,
					   list,
					   cancellable,
					   error);
	if (!ret)
		goto out;
	gs_plugin_list_free (list);
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
	gchar *profile_id;
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
		profile_id = g_strdup_printf ("GsPlugin::%s(%s)",
					      plugin->name, function_name);
		gs_profile_start (plugin_loader->priv->profile, profile_id);
		plugin_func (plugin);
		gs_profile_stop (plugin_loader->priv->profile, profile_id);
		g_free (profile_id);
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
	plugin->status_update_fn = gs_plugin_loader_status_update_cb;
	plugin->status_update_user_data = plugin_loader;
	plugin->profile = g_object_ref (plugin_loader->priv->profile);
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
	if ((*pa)->priority < (*pb)->priority)
		return -1;
	if ((*pa)->priority > (*pb)->priority)
		return 1;
	return 0;
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

	g_return_val_if_fail (plugin_loader->priv->location != NULL, FALSE);

	/* search in the plugin directory for plugins */
	gs_profile_start (plugin_loader->priv->profile, "GsPlugin::setup");
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

	/* run the plugins */
	gs_plugin_loader_run (plugin_loader, "gs_plugin_initialize");
out:
	gs_profile_stop (plugin_loader->priv->profile, "GsPlugin::setup");
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_loader_dump_state:
 **/
void
gs_plugin_loader_dump_state (GsPluginLoader *plugin_loader)
{
	GsPlugin *plugin;
	guint i;

	/* print what the priorities are */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		g_debug ("[%s]\t%.1f\t->\t%s",
			 plugin->enabled ? "enabled" : "disabled",
			 plugin->priority,
			 plugin->name);
	}
}

/**
 * gs_plugin_loader_plugin_free:
 **/
static void
gs_plugin_loader_plugin_free (GsPlugin *plugin)
{
	g_free (plugin->priv);
	g_free (plugin->name);
	g_object_unref (plugin->profile);
	g_module_close (plugin->module);
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
	signals [SIGNAL_PENDING_APPS_CHANGED] =
		g_signal_new ("pending-apps-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, pending_apps_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (GsPluginLoaderPrivate));
}

/**
 * gs_plugin_loader_init:
 **/
static void
gs_plugin_loader_init (GsPluginLoader *plugin_loader)
{
	const gchar *tmp;

	plugin_loader->priv = GS_PLUGIN_LOADER_GET_PRIVATE (plugin_loader);
	plugin_loader->priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_plugin_loader_plugin_free);
	plugin_loader->priv->status_last = GS_PLUGIN_STATUS_LAST;
	plugin_loader->priv->pending_apps = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	plugin_loader->priv->profile = gs_profile_new ();
	plugin_loader->priv->app_cache = g_hash_table_new_full (g_str_hash,
								 g_str_equal,
								 NULL,
								 (GFreeFunc) g_object_unref);

	g_mutex_init (&plugin_loader->priv->pending_apps_mutex);
	g_mutex_init (&plugin_loader->priv->app_cache_mutex);

	/* application start */
	gs_profile_start (plugin_loader->priv->profile, "GsPluginLoader");

	/* by default we only show project-less apps or compatible projects */
	tmp = g_getenv ("GNOME_SOFTWARE_COMPATIBLE_PROJECTS");
	if (tmp == NULL)
		tmp = "GNOME";
	plugin_loader->priv->compatible_projects = g_strsplit (tmp, ",", -1);
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

	/* application stop */
	gs_profile_stop (plugin_loader->priv->profile, "GsPluginLoader");

	/* run the plugins */
	gs_plugin_loader_run (plugin_loader, "gs_plugin_destroy");

	g_object_unref (plugin_loader->priv->profile);
	g_strfreev (plugin_loader->priv->compatible_projects);
	g_hash_table_unref (plugin_loader->priv->app_cache);
	g_ptr_array_unref (plugin_loader->priv->pending_apps);
	g_ptr_array_unref (plugin_loader->priv->plugins);
	g_free (plugin_loader->priv->location);

	g_mutex_clear (&plugin_loader->priv->pending_apps_mutex);
	g_mutex_clear (&plugin_loader->priv->app_cache_mutex);

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

/* vim: set noexpandtab: */
