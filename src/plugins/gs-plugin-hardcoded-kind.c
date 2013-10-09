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

#include <gs-plugin.h>

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "hardcoded-kind";
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 50.0f;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GsApp *app;
	guint i;
	const gchar *apps[] = {
		/* core apps */
		"baobab",
		"eog",
		"epiphany",
		"evince",
		"empathy",
		"gcalctool",
		"gedit",
		"gnome-bluetooth",
		"gnome-clocks",
		"gnome-contacts",
		"gnome-dictionary",
		"gnome-disks",
		"gnome-eog",
		"gnome-font-viewer",
		"gnome-gucharmap",
		"gnome-keyring",
		"gnome-menus",
		"gnome-packagekit",
		"gnome-screenshot",
		"gnome-session",
		"gnome-software",
		"gnome-system-log",
		"gnome-system-monitor",
		"gnome-terminal",
		"gnome-user-docs",
		"gnome-user-share",
		"gucharmap",
		"nautilus",
		"sushi",
		"totem",
		"vino",
		"yelp",
		/* input methods */
		"hangul",
		"kkc",
		"libpinyin",
		"m17n",
		NULL };

	/* just mark each one as core */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		for (i = 0; apps[i] != NULL; i++) {
			if (g_strcmp0 (apps[i], gs_app_get_id (app)) == 0) {
				gs_app_set_kind (app, GS_APP_KIND_SYSTEM);
				break;
			}
		}
	}
	return TRUE;
}
