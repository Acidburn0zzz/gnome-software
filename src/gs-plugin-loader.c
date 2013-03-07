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

#include "gs-plugin-loader.h"
#include "gs-plugin.h"

static void	gs_plugin_loader_finalize	(GObject	*object);

#define GS_PLUGIN_LOADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_PLUGIN_LOADER, GsPluginLoaderPrivate))

struct GsPluginLoaderPrivate
{
	GPtrArray		*plugins;
	gchar			*location;
};

G_DEFINE_TYPE (GsPluginLoader, gs_plugin_loader, G_TYPE_OBJECT)

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
		ret = plugin_func (plugin, list, error);
		if (!ret)
			goto out;
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}
out:
	return ret;
}

/**
 * gs_plugin_loader_run_results:
 **/
static GList *
gs_plugin_loader_run_results (GsPluginLoader *plugin_loader,
			      const gchar *function_name,
			      GError **error)
{
	gboolean ret;
	GList *list = NULL;
	GsPlugin *plugin;
	GsPluginResultsFunc plugin_func = NULL;
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
		ret = plugin_func (plugin, &list, error);
		if (!ret)
			goto out;
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader, list, error);
	if (!ret)
		goto out;

	/* success */
out:
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

/**
 * gs_plugin_loader_get_updates:
 **/
GList *
gs_plugin_loader_get_updates (GsPluginLoader *plugin_loader, GError **error)
{
	GList *list;
	GList *l;
	GsApp *app;
	GsApp *app_tmp;
	GString *str_id = NULL;
	GString *str_summary = NULL;
	gboolean has_os_update = FALSE;

	list = gs_plugin_loader_run_results (plugin_loader,
					     "gs_plugin_add_updates",
					     error);
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no updates to show");
		goto out;
	}

	/* coalesce all packages down into one os-update */
	for (l = list; l != NULL; l = l->next) {
		app_tmp = GS_APP (l->data);
		if (gs_app_get_kind (app_tmp) == GS_APP_KIND_PACKAGE) {
			has_os_update = TRUE;
			break;
		}
	}

	/* smush them all together */
	if (has_os_update) {
		str_summary = g_string_new ("This updates the system:\n");
		str_id = g_string_new ("os-update:");
		for (l = list; l != NULL; l = l->next) {
			app_tmp = GS_APP (l->data);
			if (gs_app_get_kind (app_tmp) != GS_APP_KIND_PACKAGE)
				continue;
			g_string_append_printf (str_id, "%s,",
						gs_app_get_id (app_tmp));
			g_string_append_printf (str_summary, "%s\n",
						gs_app_get_summary (app_tmp));
		}
		g_string_truncate (str_id, str_id->len - 1);
		g_string_truncate (str_summary, str_summary->len - 1);

		/* create new meta object */
		app = gs_app_new (str_id->str);
		gs_app_set_kind (app, GS_APP_KIND_OS_UPDATE);
		gs_app_set_name (app, "OS Update");
		gs_app_set_summary (app, str_summary->str);
		gs_plugin_add_app (&list, app);

		/* remove any packages that are not proper applications or
		 * OS updates */
		list = gs_plugin_loader_remove_invalid (list);
	}

out:
	if (str_id != NULL)
		g_string_free (str_id, TRUE);
	if (str_summary != NULL)
		g_string_free (str_summary, TRUE);
	return list;
}

/**
 * gs_plugin_loader_get_installed:
 **/
GList *
gs_plugin_loader_get_installed (GsPluginLoader *plugin_loader, GError **error)
{
	GList *list;
	list = gs_plugin_loader_run_results (plugin_loader,
					     "gs_plugin_add_installed",
					     error);
	list = gs_plugin_loader_remove_invalid (list);
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no installed packages to show");
		goto out;
	}
out:
	return list;
}

/**
 * gs_plugin_loader_get_popular:
 **/
GList *
gs_plugin_loader_get_popular (GsPluginLoader *plugin_loader, GError **error)
{
	GList *list;
	list = gs_plugin_loader_run_results (plugin_loader,
					     "gs_plugin_add_popular",
					     error);
	list = gs_plugin_loader_remove_invalid (list);
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no popular apps to show");
		goto out;
	}
out:
	return list;
}

/**
 * gs_plugin_loader_search:
 **/
GList *
gs_plugin_loader_search (GsPluginLoader *plugin_loader, const gchar *value, GError **error)
{
	const gchar *function_name = "gs_plugin_add_search";
	gboolean ret;
	GList *list = NULL;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
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
		ret = plugin_func (plugin, value, &list, error);
		if (!ret)
			goto out;
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader, list, error);
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
	return list;
}


/**
 * gs_plugin_loader_run_action:
 **/
static gboolean
gs_plugin_loader_run_action (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     const gchar *function_name,
			     GError **error)
{
	gboolean exists;
	gboolean ret = FALSE;
	GError *error_local = NULL;
	GsPluginActionFunc plugin_func = NULL;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < plugin_loader->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->priv->plugins, i);
		if (!plugin->enabled)
			continue;
		exists = g_module_symbol (plugin->module,
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		g_debug ("run %s on %s", function_name,
			 g_module_name (plugin->module));
		g_timer_start (plugin->timer);
		ret = plugin_func (plugin, app, &error_local);
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
		g_debug ("%s(%s) took %.0fms",
			 plugin->name,
			 function_name,
			 g_timer_elapsed (plugin->timer, NULL) * 1000);
	}

	/* nothing ran */
	if (!ret) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no plugin could handle %s",
			     function_name);
	}
out:
	return ret;
}

/**
 * gs_plugin_loader_app_update:
 **/
gboolean
gs_plugin_loader_app_update (GsPluginLoader *plugin_loader, GsApp *app, GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_update",
					    error);
}

/**
 * gs_plugin_loader_app_install:
 **/
gboolean
gs_plugin_loader_app_install (GsPluginLoader *plugin_loader, GsApp *app, GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_install",
					    error);
}

/**
 * gs_plugin_loader_app_remove:
 **/
gboolean
gs_plugin_loader_app_remove (GsPluginLoader *plugin_loader, GsApp *app, GError **error)
{
	return gs_plugin_loader_run_action (plugin_loader,
					    app,
					    "gs_plugin_app_remove",
					    error);
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
	plugin = g_new0 (GsPlugin, 1);
	plugin->enabled = FALSE;
	plugin->module = module;
	plugin->pixbuf_size = 64;
	plugin->priority = plugin_prio (plugin);
	plugin->name = g_strdup (plugin_name ());
	plugin->timer = g_timer_new ();
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
	g_free (plugin);
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
