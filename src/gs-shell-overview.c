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

#include <glib/gi18n.h>

#include "gs-shell.h"
#include "gs-shell-overview.h"
#include "gs-app.h"
#include "gs-category.h"
#include "gs-popular-tile.h"
#include "gs-feature-tile.h"
#include "gs-utils.h"

static void	gs_shell_overview_finalize	(GObject	*object);

#define GS_SHELL_OVERVIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_OVERVIEW, GsShellOverviewPrivate))

struct GsShellOverviewPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
        GsShell                 *shell;
        gint                     refresh_count;
        GtkCssProvider          *feature_style;
};

G_DEFINE_TYPE (GsShellOverview, gs_shell_overview, G_TYPE_OBJECT)

enum {
        SIGNAL_REFRESHED,
        SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_shell_overview_invalidate:
 **/
void
gs_shell_overview_invalidate (GsShellOverview *shell_overview)
{
	shell_overview->priv->cache_valid = FALSE;
}

static void
popular_tile_clicked (GsPopularTile *tile, gpointer data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (data);
	GsApp *app;

	app = gs_popular_tile_get_app (tile);
        gs_shell_show_app (shell->priv->shell, app);
}

/**
 * gs_shell_overview_get_popular_cb:
 **/
static void
gs_shell_overview_get_popular_cb (GObject *source_object,
			GAsyncResult *res,
			gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	gint i;
	GtkWidget *tile;
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *grid;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		g_warning ("failed to get popular apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	for (l = list, i = 0; l != NULL && i < 6; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = gs_popular_tile_new (app);
	        g_signal_connect (tile, "clicked",
			  G_CALLBACK (popular_tile_clicked), shell_overview);
                gtk_box_pack_start (GTK_BOX (grid), tile, TRUE, TRUE, 0);
	}
out:
        priv->refresh_count--;
        if (priv->refresh_count == 0)
                g_signal_emit (shell_overview, signals[SIGNAL_REFRESHED], 0);
}

static void
category_tile_clicked (GtkButton *button, gpointer data)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (data);
        GsCategory *category;

	category = GS_CATEGORY (g_object_get_data (G_OBJECT (button), "category"));
        gs_shell_show_category (shell_overview->priv->shell, category);
}

static GtkWidget *
create_category_tile (GsShellOverview *shell_overview, GsCategory *category)
{
	GtkWidget *button, *label;

	button = gtk_button_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "view");
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "tile");
	label = gtk_label_new (gs_category_get_name (category));
	g_object_set (label, "margin", 12, "xalign", 0, NULL);
	gtk_container_add (GTK_CONTAINER (button), label);
	gtk_widget_show_all (button);
	g_object_set_data_full (G_OBJECT (button), "category", g_object_ref (category), g_object_unref);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (category_tile_clicked), shell_overview);

	return button;
}

static void
feature_tile_clicked (GsFeatureTile *tile, gpointer data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (data);
        GsApp *app;

        app = gs_feature_tile_get_app (tile);
        gs_shell_show_app (shell->priv->shell, app);
}

static void
gs_shell_overview_get_featured_cb (GObject *source_object,
               	 		   GAsyncResult *res,
	         		   gpointer user_data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *tile;
	GtkWidget *box;
        GList *list;
        GError *error = NULL;
        GsApp *app;

	list = gs_plugin_loader_get_featured_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get featured apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* at the moment, we only care about the first app */
	app = GS_APP (list->data);
        box = GTK_WIDGET (gtk_builder_get_object (priv->builder, "feature"));
        tile = gs_feature_tile_new (app);
	g_signal_connect (tile, "clicked",
			  G_CALLBACK (feature_tile_clicked), shell);

        gs_container_remove_all (GTK_CONTAINER (box));
        gtk_container_add (GTK_CONTAINER (box), tile);

out:
	g_list_free (list);

        priv->refresh_count--;
        if (priv->refresh_count == 0)
                g_signal_emit (shell, signals[SIGNAL_REFRESHED], 0);
}

/**
 * gs_shell_overview_get_categories_cb:
 **/
static void
gs_shell_overview_get_categories_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GError *error = NULL;
	gint i;
	GList *l;
	GList *list;
	GsCategory *cat;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GtkWidget *grid;
	GtkWidget *tile;

	list = gs_plugin_loader_get_categories_finish (plugin_loader,
						       res,
						       &error);
	if (list == NULL) {
		g_warning ("failed to get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}
	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
	for (l = list, i = 0; l; l = l->next, i++) {
		cat = GS_CATEGORY (l->data);
		tile = create_category_tile (shell_overview, cat);
		gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
	}
        g_list_free_full (list, g_object_unref);
out:
	priv->cache_valid = TRUE;
}

/**
 * gs_shell_overview_refresh:
 **/
void
gs_shell_overview_refresh (GsShellOverview *shell_overview, gboolean scroll_up)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GtkWidget *widget;
	GtkWidget *grid;
        GtkAdjustment *adj;

        if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_OVERVIEW) {
                widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
                gtk_widget_show (widget);
	        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	        gtk_widget_show (widget);
	        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
                gtk_entry_set_text (GTK_ENTRY (widget), "");
        }

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_overview"));
        if (scroll_up) {
                adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (widget));
                gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
        }

        if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_OVERVIEW) {
                gs_grab_focus_when_mapped (widget);
        }

	/* no need to refresh */
	if (priv->cache_valid)
		return;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	gs_container_remove_all (GTK_CONTAINER (grid));

        priv->refresh_count = 2;

	/* get featured apps */
	gs_plugin_loader_get_featured_async (priv->plugin_loader,
					     priv->cancellable,
					     gs_shell_overview_get_featured_cb,
					     shell_overview);

	/* get popular apps */
	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    priv->cancellable,
					    gs_shell_overview_get_popular_cb,
					    shell_overview);

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
	gs_container_remove_all (GTK_CONTAINER (grid));

	/* get categories */
	gs_plugin_loader_get_categories_async (priv->plugin_loader,
					       priv->cancellable,
					       gs_shell_overview_get_categories_cb,
					       shell_overview);
}

void
gs_shell_overview_setup (GsShellOverview *shell_overview,
                         GsShell *shell,
			 GsPluginLoader *plugin_loader,
			 GtkBuilder *builder,
			 GCancellable *cancellable)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;

	g_return_if_fail (GS_IS_SHELL_OVERVIEW (shell_overview));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

        /* avoid a ref cycle */
        priv->shell = shell;

        priv->feature_style = gtk_css_provider_new ();
        gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                   GTK_STYLE_PROVIDER (priv->feature_style),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/**
 * gs_shell_overview_class_init:
 **/
static void
gs_shell_overview_class_init (GsShellOverviewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_overview_finalize;

        signals [SIGNAL_REFRESHED] =
                g_signal_new ("refreshed",
                              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsShellOverviewClass, refreshed),
                              NULL, NULL, g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (GsShellOverviewPrivate));
}

/**
 * gs_shell_overview_init:
 **/
static void
gs_shell_overview_init (GsShellOverview *shell_overview)
{
	shell_overview->priv = GS_SHELL_OVERVIEW_GET_PRIVATE (shell_overview);
}

/**
 * gs_shell_overview_finalize:
 **/
static void
gs_shell_overview_finalize (GObject *object)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (object);
	GsShellOverviewPrivate *priv = shell_overview->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);
        g_object_unref (priv->feature_style);

	G_OBJECT_CLASS (gs_shell_overview_parent_class)->finalize (object);
}

/**
 * gs_shell_overview_new:
 **/
GsShellOverview *
gs_shell_overview_new (void)
{
	GsShellOverview *shell_overview;
	shell_overview = g_object_new (GS_TYPE_SHELL_OVERVIEW, NULL);
	return GS_SHELL_OVERVIEW (shell_overview);
}
