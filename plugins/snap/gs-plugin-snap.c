/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Canonical Ltd
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

#include <gio/gdesktopappinfo.h>
#include <snapd-glib/snapd-glib.h>
#include <gnome-software.h>

struct GsPluginData {
	SnapdClient		*client;
	SnapdSystemConfinement	 system_confinement;
	GsAuth			*auth;
	GHashTable		*store_snaps;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	const gchar *old_user_agent;
	g_autofree gchar *user_agent = NULL;
	g_autoptr (GError) error = NULL;

	priv->client = snapd_client_new ();
	if (!snapd_client_connect_sync (priv->client, NULL, &error)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}
	old_user_agent = snapd_client_get_user_agent (priv->client);
	user_agent = g_strdup_printf ("%s %s", gs_user_agent (), old_user_agent);
	snapd_client_set_user_agent (priv->client, user_agent);

	priv->store_snaps = g_hash_table_new_full (g_str_hash, g_str_equal,
						   g_free, (GDestroyNotify) g_object_unref);

	priv->auth = gs_auth_new ("snapd");
	gs_auth_set_provider_name (priv->auth, "Snap Store");
	gs_auth_set_provider_schema (priv->auth, "com.ubuntu.UbuntuOne.GnomeSoftware");
	gs_plugin_add_auth (plugin, priv->auth);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "desktop-categories");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "ubuntu-reviews");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");

	/* Override hardcoded popular apps */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "hardcoded-popular");

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Snap");
}

static void
snapd_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* custom to this plugin */
	if (error->domain == SNAPD_ERROR) {
		switch (error->code) {
		case SNAPD_ERROR_AUTH_DATA_REQUIRED:
			error->code = GS_PLUGIN_ERROR_AUTH_REQUIRED;
			g_free (error->message);
			error->message = g_strdup ("Requires authentication with @snapd");
			break;
		case SNAPD_ERROR_TWO_FACTOR_REQUIRED:
			error->code = GS_PLUGIN_ERROR_PIN_REQUIRED;
			break;
		case SNAPD_ERROR_AUTH_DATA_INVALID:
		case SNAPD_ERROR_TWO_FACTOR_INVALID:
			error->code = GS_PLUGIN_ERROR_AUTH_INVALID;
			break;
		case SNAPD_ERROR_CONNECTION_FAILED:
		case SNAPD_ERROR_WRITE_FAILED:
		case SNAPD_ERROR_READ_FAILED:
		case SNAPD_ERROR_BAD_REQUEST:
		case SNAPD_ERROR_BAD_RESPONSE:
		case SNAPD_ERROR_PERMISSION_DENIED:
		case SNAPD_ERROR_FAILED:
		case SNAPD_ERROR_TERMS_NOT_ACCEPTED:
		case SNAPD_ERROR_PAYMENT_NOT_SETUP:
		case SNAPD_ERROR_PAYMENT_DECLINED:
		case SNAPD_ERROR_ALREADY_INSTALLED:
		case SNAPD_ERROR_NOT_INSTALLED:
		case SNAPD_ERROR_NO_UPDATE_AVAILABLE:
		case SNAPD_ERROR_PASSWORD_POLICY_ERROR:
		case SNAPD_ERROR_NEEDS_DEVMODE:
		case SNAPD_ERROR_NEEDS_CLASSIC:
		case SNAPD_ERROR_NEEDS_CLASSIC_SYSTEM:
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else {
		g_warning ("can't reliably fixup error from domain %s",
			   g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
	}
	error->domain = GS_PLUGIN_ERROR;
}

static void
load_auth (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsAuth *auth;
	const gchar *serialized_macaroon;
	g_autoptr(GVariant) macaroon_variant = NULL;
	const gchar *macaroon;
	g_auto(GStrv) discharges = NULL;
	g_autoptr(SnapdAuthData) auth_data = NULL;

	auth = gs_plugin_get_auth_by_id (plugin, "snapd");
	if (auth == NULL)
		return;

	serialized_macaroon = gs_auth_get_metadata_item (auth, "macaroon");
	if (serialized_macaroon == NULL)
		return;

	macaroon_variant = g_variant_parse (G_VARIANT_TYPE ("(sas)"),
					    serialized_macaroon,
					    NULL,
					    NULL,
					    NULL);
	if (macaroon_variant == NULL)
		return;

	g_variant_get (macaroon_variant, "(&s^as)", &macaroon, &discharges);
	auth_data = snapd_auth_data_new (macaroon, discharges);
	snapd_client_set_auth_data (priv->client, auth_data);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(SnapdSystemInformation) system_information = NULL;

	system_information = snapd_client_get_system_information_sync (priv->client, cancellable, error);
	if (system_information == NULL)
		return FALSE;
	priv->system_confinement = snapd_system_information_get_confinement (system_information);

	/* load from disk */
	gs_auth_add_metadata (priv->auth, "macaroon", NULL);
	if (!gs_auth_store_load (priv->auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;
	load_auth (plugin);

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_snap_set_app_pixbuf_from_data (GsApp *app, const gchar *buf, gsize count, GError **error)
{
	g_autoptr(GdkPixbufLoader) loader = NULL;
	g_autoptr(GError) error_local = NULL;

	loader = gdk_pixbuf_loader_new ();
	if (!gdk_pixbuf_loader_write (loader, (const guchar *) buf, count, &error_local)) {
		g_debug ("icon_data[%" G_GSIZE_FORMAT "]=%s", count, buf);
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to write: %s",
			     error_local->message);
		return FALSE;
	}
	if (!gdk_pixbuf_loader_close (loader, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to close: %s",
			     error_local->message);
		return FALSE;
	}
	gs_app_set_pixbuf (app, gdk_pixbuf_loader_get_pixbuf (loader));
	return TRUE;
}

static GPtrArray *
find_snaps (GsPlugin *plugin, SnapdFindFlags flags, const gchar *section, const gchar *query, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	snaps = snapd_client_find_section_sync (priv->client, flags, section, query, NULL, cancellable, error);
	if (snaps == NULL) {
		snapd_error_convert (error);
		return NULL;
	}

	/* cache results */
	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = snaps->pdata[i];
		g_hash_table_insert (priv->store_snaps, g_strdup (snapd_snap_get_name (snap)), g_object_ref (snap));
	}

	return g_steal_pointer (&snaps);
}

static GsApp *
snap_to_app (GsPlugin *plugin, SnapdSnap *snap)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsApp *app;

	/* create a unique ID for deduplication, TODO: branch? */
	app = gs_app_new (snapd_snap_get_name (snap));
	switch (snapd_snap_get_snap_type (snap)) {
	case SNAPD_SNAP_TYPE_APP:
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		break;
	case SNAPD_SNAP_TYPE_KERNEL:
	case SNAPD_SNAP_TYPE_GADGET:
	case SNAPD_SNAP_TYPE_OS:
		gs_app_set_kind (app, AS_APP_KIND_RUNTIME);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
		break;
        default:
	case SNAPD_SNAP_TYPE_UNKNOWN:
                break;
	}
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_SNAP);
	gs_app_set_management_plugin (app, "snap");
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
	if (gs_plugin_check_distro_id (plugin, "ubuntu"))
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	if (priv->system_confinement == SNAPD_SYSTEM_CONFINEMENT_STRICT && snapd_snap_get_confinement (snap) == SNAPD_CONFINEMENT_STRICT)
		gs_app_add_kudo (app, GS_APP_KUDO_SANDBOXED);

	return app;
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autofree gchar *scheme = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	g_autoptr(GsApp) app = NULL;

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "snap") != 0)
		return TRUE;

	/* create app */
	path = gs_utils_get_url_path (url);
	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_MATCH_NAME, NULL, path, cancellable, NULL);
	if (snaps == NULL || snaps->len < 1)
		return TRUE;

	app = snap_to_app (plugin, g_ptr_array_index (snaps, 0));
	gs_app_list_add (list, app);

	return TRUE;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_clear_object (&priv->auth);
	g_clear_pointer (&priv->store_snaps, g_hash_table_unref);
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_NONE, "featured", NULL, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	for (i = 0; i < snaps->len; i++) {
		g_autoptr(GsApp) app = snap_to_app (plugin, g_ptr_array_index (snaps, i));
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	snaps = snapd_client_list_sync (priv->client, cancellable, error);
	if (snaps == NULL) {
		snapd_error_convert (error);
		return FALSE;
	}

	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = g_ptr_array_index (snaps, i);
		g_autoptr(GsApp) app = NULL;

		if (snapd_snap_get_status (snap) != SNAPD_SNAP_STATUS_ACTIVE)
			continue;

		app = snap_to_app (plugin, snap);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autofree gchar *query = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	query = g_strjoinv (" ", values);
	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_NONE, NULL, query, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	for (i = 0; i < snaps->len; i++) {
		g_autoptr(GsApp) app = snap_to_app (plugin, g_ptr_array_index (snaps, i));
		gs_app_list_add (list, app);
	}

	return TRUE;
}

static gboolean
load_icon (GsPlugin *plugin, GsApp *app, const gchar *icon_url, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (icon_url == NULL || g_strcmp0 (icon_url, "") == 0) {
		g_autoptr(AsIcon) icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "package-x-generic");
		gs_app_add_icon (app, icon);
		return TRUE;
	}

	/* icon is optional, either loaded from snapd or from a URL */
	if (g_str_has_prefix (icon_url, "/")) {
		g_autoptr(SnapdIcon) icon = NULL;

		icon = snapd_client_get_icon_sync (priv->client, gs_app_get_id (app), cancellable, error);
		if (icon == NULL) {
			snapd_error_convert (error);
			return FALSE;
		}

		if (!gs_plugin_snap_set_app_pixbuf_from_data (app,
							      g_bytes_get_data (snapd_icon_get_data (icon), NULL),
							      g_bytes_get_size (snapd_icon_get_data (icon)),
							      error)) {
			g_prefix_error (error, "Failed to load %s: ", icon_url);
			return FALSE;
		}
	} else {
		g_autoptr(AsIcon) icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_REMOTE);
		as_icon_set_url (icon, icon_url);
		gs_app_add_icon (app, icon);
	}

	return TRUE;
}

static SnapdSnap *
get_store_snap (GsPlugin *plugin, const gchar *name, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	SnapdSnap *snap = NULL;
	g_autoptr(GPtrArray) snaps = NULL;

	/* use cached version if available */
	snap = g_hash_table_lookup (priv->store_snaps, name);
	if (snap != NULL)
		return g_object_ref (snap);

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_MATCH_NAME, NULL, name, cancellable, error);
	if (snaps == NULL || snaps->len < 1)
		return NULL;

	return g_object_ref (g_ptr_array_index (snaps, 0));
}

static void
find_launch_app (GsApp *app, SnapdSnap *local_snap)
{
	const char *snap_name;
	GPtrArray *apps;
	guint i;
	const char *launch_name = NULL;
	const char *launch_desktop = NULL;

	snap_name = snapd_snap_get_name (local_snap);
	apps = snapd_snap_get_apps (local_snap);

	/* Pick the "main" app from the snap.  In order of
	 * preference, we want to pick:
	 *
	 *   1. the main app, provided it has a desktop file
	 *   2. the first app with a desktop file
	 *   3. the main app
	 *   4. the first app
	 *
	 * The "main app" is one whose name matches the snap name.
	 */
	for (i = 0; i < apps->len; i++) {
		SnapdApp *snap_app = apps->pdata[i];
		const char *app_name = snapd_app_get_name (snap_app);
		const char *app_desktop = snapd_app_get_desktop_file (snap_app);
		gboolean is_main_app = !g_strcmp0(snap_name, app_name);

		if (launch_name == NULL || is_main_app) {
			launch_name = app_name;
		}
		if (launch_desktop == NULL || is_main_app) {
			if (app_desktop != NULL) {
				launch_desktop = app_desktop;
			}
		}
	}

	gs_app_set_metadata (app, "snap::launch-name", launch_name);
	gs_app_set_metadata (app, "snap::launch-desktop", launch_desktop);

	if (!launch_name)
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id, *icon_url = NULL;
	g_autoptr(SnapdSnap) local_snap = NULL;
	g_autoptr(SnapdSnap) store_snap = NULL;

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	id = gs_app_get_id (app);
	if (id == NULL)
		id = gs_app_get_source_default (app);

	/* get information from installed snaps */
	local_snap = snapd_client_list_one_sync (priv->client, id, cancellable, NULL);
	if (local_snap != NULL) {
		const gchar *name;
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		name = snapd_snap_get_title (local_snap);
		if (name == NULL || g_strcmp0 (name, "") == 0)
			name = snapd_snap_get_name (local_snap);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, name);
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, snapd_snap_get_summary (local_snap));
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, snapd_snap_get_description (local_snap));
		gs_app_set_version (app, snapd_snap_get_version (local_snap));
		gs_app_set_size_installed (app, snapd_snap_get_installed_size (local_snap));
		gs_app_set_install_date (app, g_date_time_to_unix (snapd_snap_get_install_date (local_snap)));
		gs_app_set_developer_name (app, snapd_snap_get_developer (local_snap));
		icon_url = snapd_snap_get_icon (local_snap);
		if (g_strcmp0 (icon_url, "") == 0)
			icon_url = NULL;

		find_launch_app (app, local_snap);
	}

	/* get information from snap store */
	store_snap = get_store_snap (plugin, id, cancellable, NULL);
	if (store_snap != NULL) {
		GPtrArray *screenshots;
		const gchar *name, *screenshot_url = NULL;

		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

		name = snapd_snap_get_title (store_snap);
		if (name == NULL || g_strcmp0 (name, "") == 0)
			name = snapd_snap_get_name (store_snap);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, name);
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, snapd_snap_get_summary (store_snap));
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, snapd_snap_get_description (store_snap));
		gs_app_set_version (app, snapd_snap_get_version (store_snap));
		gs_app_set_size_download (app, snapd_snap_get_download_size (store_snap));
		gs_app_set_developer_name (app, snapd_snap_get_developer (store_snap));
		if (icon_url == NULL) {
			icon_url = snapd_snap_get_icon (store_snap);
			if (g_strcmp0 (icon_url, "") == 0)
				icon_url = NULL;
		}

		screenshots = snapd_snap_get_screenshots (store_snap);
		if (screenshots != NULL && gs_app_get_screenshots (app)->len == 0) {
			guint i;

			for (i = 0; i < screenshots->len; i++) {
				SnapdScreenshot *screenshot = screenshots->pdata[i];
				g_autoptr(AsScreenshot) ss = NULL;
				g_autoptr(AsImage) image = NULL;

				ss = as_screenshot_new ();
				as_screenshot_set_kind (ss, AS_SCREENSHOT_KIND_NORMAL);
				image = as_image_new ();
				as_image_set_url (image, snapd_screenshot_get_url (screenshot));
				as_image_set_kind (image, AS_IMAGE_KIND_SOURCE);
				as_image_set_width (image, snapd_screenshot_get_width (screenshot));
				as_image_set_height (image, snapd_screenshot_get_height (screenshot));
				as_screenshot_add_image (ss, image);
				gs_app_add_screenshot (app, ss);

				/* fall back to the screenshot */
				if (screenshot_url == NULL)
					screenshot_url = snapd_screenshot_get_url (screenshot);
			}
		}

		/* use some heuristics to guess the application origin */
		if (gs_app_get_origin_hostname (app) == NULL) {
			if (icon_url != NULL && !g_str_has_prefix (icon_url, "/"))
				gs_app_set_origin_hostname (app, icon_url);
			else if (screenshot_url != NULL)
				gs_app_set_origin_hostname (app, screenshot_url);
		}
	}

	/* load icon if requested */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) {
		if (!load_icon (plugin, app, icon_url, cancellable, error)) {
			snapd_error_convert (error);
			return FALSE;
		}
	}

	return TRUE;
}

static void
progress_cb (SnapdClient *client, SnapdChange *change, gpointer deprecated, gpointer user_data)
{
	GsApp *app = user_data;
	GPtrArray *tasks;
	guint i;
	gint64 done = 0, total = 0;

	tasks = snapd_change_get_tasks (change);
	for (i = 0; i < tasks->len; i++) {
		SnapdTask *task = tasks->pdata[i];
		done += snapd_task_get_progress_done (task);
		total += snapd_task_get_progress_total (task);
	}

	gs_app_set_progress (app, (guint) (100 * done / total));
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!snapd_client_install2_sync (priv->client, SNAPD_INSTALL_FLAGS_NONE, gs_app_get_id (app), NULL, NULL, progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

// Check if an app is graphical by checking if it uses a known GUI interface.
// This doesn't necessarily mean that every binary uses this interfaces, but is probably true.
// https://bugs.launchpad.net/bugs/1595023
static gboolean
is_graphical (GsPlugin *plugin, GsApp *app, GCancellable *cancellable)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) plugs = NULL;
	guint i;
	g_autoptr(GError) error = NULL;

	if (!snapd_client_get_interfaces_sync (priv->client, &plugs, NULL, cancellable, &error)) {
		g_warning ("Failed to check interfaces: %s", error->message);
		return FALSE;
	}

	for (i = 0; i < plugs->len; i++) {
		SnapdPlug *plug = plugs->pdata[i];
		const gchar *interface;

		// Only looks at the plugs for this snap
		if (g_strcmp0 (snapd_plug_get_snap (plug), gs_app_get_id (app)) != 0)
			continue;

		interface = snapd_plug_get_interface (plug);
		if (interface == NULL)
			continue;

		if (g_strcmp0 (interface, "unity7") == 0 || g_strcmp0 (interface, "x11") == 0 || g_strcmp0 (interface, "mir") == 0)
			return TRUE;
	}

	return FALSE;
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *launch_name;
	const gchar *launch_desktop;
	g_autoptr(GAppInfo) info = NULL;

	/* We can only launch apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	launch_name = gs_app_get_metadata_item (app, "snap::launch-name");
	launch_desktop = gs_app_get_metadata_item (app, "snap::launch-desktop");
	if (!launch_name)
		return TRUE;

	if (launch_desktop) {
		info = (GAppInfo *)g_desktop_app_info_new_from_filename (launch_desktop);
	} else {
		g_autofree gchar *binary_name = NULL;
		GAppInfoCreateFlags flags = G_APP_INFO_CREATE_NONE;

		if (g_strcmp0 (launch_name, gs_app_get_id (app)) == 0)
			binary_name = g_strdup_printf ("/snap/bin/%s", launch_name);
		else
			binary_name = g_strdup_printf ("/snap/bin/%s.%s", gs_app_get_id (app), launch_name);

		if (!is_graphical (plugin, app, cancellable))
			flags |= G_APP_INFO_CREATE_NEEDS_TERMINAL;
		info = g_app_info_create_from_commandline (binary_name, NULL, flags, error);
	}

	if (info == NULL)
		return FALSE;

	return g_app_info_launch (info, NULL, NULL, error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* We can only remove apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	if (!snapd_client_remove_sync (priv->client, gs_app_get_id (app), progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	return TRUE;
}

gboolean
gs_plugin_auth_login (GsPlugin *plugin, GsAuth *auth,
		      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(SnapdAuthData) auth_data = NULL;
	g_autoptr(GVariant) macaroon_variant = NULL;
	g_autofree gchar *serialized_macaroon = NULL;

	if (auth != priv->auth)
		return TRUE;

	auth_data = snapd_login_sync (gs_auth_get_username (auth), gs_auth_get_password (auth), gs_auth_get_pin (auth), NULL, error);
	if (auth_data == NULL) {
		snapd_error_convert (error);
		return FALSE;
	}

	snapd_client_set_auth_data (priv->client, auth_data);

	macaroon_variant = g_variant_new ("(s^as)",
					  snapd_auth_data_get_macaroon (auth_data),
					  snapd_auth_data_get_discharges (auth_data));
	serialized_macaroon = g_variant_print (macaroon_variant, FALSE);
	gs_auth_add_metadata (auth, "macaroon", serialized_macaroon);

	/* store */
	if (!gs_auth_store_save (auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;

	gs_auth_add_flags (priv->auth, GS_AUTH_FLAG_VALID);

	return TRUE;
}

gboolean
gs_plugin_auth_lost_password (GsPlugin *plugin, GsAuth *auth,
			      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	// FIXME: snapd might not be using Ubuntu One accounts
	// https://bugs.launchpad.net/bugs/1598667
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "do online using @https://login.ubuntu.com/+forgot_password");
	return FALSE;
}

gboolean
gs_plugin_auth_register (GsPlugin *plugin, GsAuth *auth,
			 GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	// FIXME: snapd might not be using Ubuntu One accounts
	// https://bugs.launchpad.net/bugs/1598667
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "do online using @https://login.ubuntu.com/+login");
	return FALSE;
}
