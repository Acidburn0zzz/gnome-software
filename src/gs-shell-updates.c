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
#include "gs-shell-updates.h"
#include "gs-utils.h"
#include "gs-offline-updates.h"
#include "gs-app.h"
#include "gs-app-widget.h"
#include "gs-markdown.h"

static void	gs_shell_updates_finalize	(GObject	*object);

#define GS_SHELL_UPDATES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_UPDATES, GsShellUpdatesPrivate))

struct GsShellUpdatesPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GtkListBox		*list_box_updates;
	gboolean		 cache_valid;
	gboolean		 waiting;
	GsShell			*shell;
	GsApp			*app;
};

enum {
	COLUMN_UPDATE_APP,
	COLUMN_UPDATE_NAME,
	COLUMN_UPDATE_VERSION,
	COLUMN_UPDATE_LAST
};

G_DEFINE_TYPE (GsShellUpdates, gs_shell_updates, G_TYPE_OBJECT)

/**
 * gs_shell_updates_invalidate:
 **/
void
gs_shell_updates_invalidate (GsShellUpdates *shell_updates)
{
	shell_updates->priv->cache_valid = FALSE;
}

/**
 * gs_shell_updates_get_updates_cb:
 **/
static void
gs_shell_updates_get_updates_cb (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsShellUpdates *shell_updates)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;

	priv->waiting = FALSE;
	priv->cache_valid = TRUE;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_updates"));
	gs_stop_spinner (GTK_SPINNER (widget));

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates_counter"));
	if (list != NULL) {
		gchar *text;
		text = g_strdup_printf ("%d", g_list_length (list));
		gtk_label_set_label (GTK_LABEL (widget), text);
		g_free (text);
		gtk_widget_show (widget);
	} else {
		gtk_label_set_label (GTK_LABEL (widget), "4");
		gtk_widget_hide (widget);
	}

	if (list != NULL &&
            gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_UPDATES &&
            gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_UPDATED)
		gtk_style_context_add_class (gtk_widget_get_style_context (widget), "needs-attention");
	else
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "needs-attention");

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_UPDATES) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_set_visible (widget, list != NULL);
	}
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no updates to show");
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
								     "stack_updates"));
			gtk_stack_set_visible_child_name (GTK_STACK (widget), "uptodate");
		} else {
			g_warning ("failed to get updates: %s", error->message);
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
								     "stack_updates"));
			gtk_stack_set_visible_child_name (GTK_STACK (widget), "failed");
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
								     "label_updates_failed"));
			gtk_label_set_label (GTK_LABEL (widget), error->message);
		}
		g_error_free (error);
		goto out;
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
							     "stack_updates"));
		gtk_stack_set_visible_child_name (GTK_STACK (widget), "view");
	}
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		widget = gs_app_widget_new ();
		gs_app_widget_set_show_update (GS_APP_WIDGET (widget), TRUE);
		gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_updates), widget);
		gtk_widget_show (widget);
	}

out:
	if (list != NULL)
		gs_plugin_list_free (list);
}

/**
 * gs_shell_updates_refresh:
 **/
void
gs_shell_updates_refresh (GsShellUpdates *shell_updates,
			  gboolean show_historical,
			  gboolean scroll_up)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;
	GtkWindow *window;
	GtkSpinner *spinner;
	GList *list;
	guint64 refine_flags;

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_UPDATES ||
            gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_UPDATED) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_show (widget);
	}

	/* set the window title to be more specific */
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	if (show_historical) {
		/* TRANSLATORS: window title to suggest that we are showing
		 * the offline updates that have just been applied */
		gtk_window_set_title (window, _("Recent Software Updates"));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (widget));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	/* no need to refresh */
	if (priv->cache_valid) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "needs-attention");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		list = gtk_container_get_children (GTK_CONTAINER (priv->list_box_updates));
		gtk_widget_set_visible (widget, list != NULL);
		g_list_free (list);
		return;
	}

	if (priv->waiting)
		return;

	gs_container_remove_all (GTK_CONTAINER (priv->list_box_updates));

	refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION;
	if (show_historical)
		refine_flags |= GS_PLUGIN_REFINE_FLAGS_USE_HISTORY;
	gs_plugin_loader_get_updates_async (priv->plugin_loader,
					    refine_flags,
					    priv->cancellable,
					    (GAsyncReadyCallback) gs_shell_updates_get_updates_cb,
					    shell_updates);

	spinner = GTK_SPINNER (gtk_builder_get_object (priv->builder, "spinner_updates"));
	gs_start_spinner (spinner);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_updates"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "spinner");

	priv->waiting = TRUE;
}

/**
 * gs_shell_updates_set_updates_description_ui:
 **/
static void
gs_shell_updates_set_updates_description_ui (GsShellUpdates *shell_updates, GsApp *app)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GsAppKind kind;
	GsMarkdown *markdown;
	GtkWidget *widget;
	gchar *tmp;
	gchar *update_desc;

	/* set window title */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	kind = gs_app_get_kind (app);
	if (kind == GS_APP_KIND_OS_UPDATE) {
		gtk_window_set_title (GTK_WINDOW (widget), gs_app_get_name (app));
	} else {
		tmp = g_strdup_printf ("%s %s",
				       gs_app_get_source_default (app),
				       gs_app_get_update_version_ui (app));
		gtk_window_set_title (GTK_WINDOW (widget), tmp);
		g_free (tmp);
	}

	/* get the update description */
	if (gs_app_get_update_details (app) == NULL) {
		/* TRANSLATORS: this is where the packager did not write a
		 * description for the update */
		update_desc = g_strdup ("No update description");
	} else {
		markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
		gs_markdown_set_smart_quoting (markdown, FALSE);
		gs_markdown_set_autocode (markdown, TRUE);
		update_desc = gs_markdown_parse (markdown, gs_app_get_update_details (app));
		g_object_unref (markdown);
	}

	/* set update header */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_update_header"));
	gtk_widget_set_visible (widget, kind == GS_APP_KIND_NORMAL || kind == GS_APP_KIND_SYSTEM);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update_details"));
	gtk_widget_set_visible (widget, kind != GS_APP_KIND_OS_UPDATE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_set_visible (widget, kind == GS_APP_KIND_OS_UPDATE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_details"));
	gtk_label_set_markup (GTK_LABEL (widget), update_desc);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_update_icon"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (widget), gs_app_get_pixbuf (app));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_name"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_name (app));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_summary"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_summary (app));
	g_free (update_desc);
}

static void
gs_shell_updates_row_activated_cb (GtkListBox *list_box,
				   GtkListBoxRow *row,
				   GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GsApp *app = NULL;
	GtkWidget *widget;

	app = GS_APP (g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "app"));
	/* setup package view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_hide (widget);
	gs_shell_updates_set_updates_description_ui (shell_updates, app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_show (widget);
}

static void
show_update_details (GsApp *app, GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GsApp *app_related;
	GsAppKind kind;
	GtkWidget *widget;

	kind = gs_app_get_kind (app);

	/* set update header */
	gs_shell_updates_set_updates_description_ui (shell_updates, app);

	/* only OS updates can go back, and only on selection */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_hide (widget);

	/* set update description */
	if (kind == GS_APP_KIND_OS_UPDATE) {
		GPtrArray *related;
		GtkListBox *list_box;
		guint i;
		GtkWidget *row, *label;

		list_box = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_update"));
		gs_container_remove_all (GTK_CONTAINER (list_box));
		related = gs_app_get_related (app);
		for (i = 0; i < related->len; i++) {
			app_related = g_ptr_array_index (related, i);
			row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
			g_object_set_data_full (G_OBJECT (row), "app", g_object_ref (app_related), g_object_unref);
			label = gtk_label_new (gs_app_get_source_default (app_related));
			g_object_set (label,
				      "margin-left", 20,
				      "margin-right", 20,
				      "margin-top", 6,
				      "margin-bottom", 6,
				      "xalign", 0.0,
				      NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
			label = gtk_label_new (gs_app_get_update_version_ui (app_related));
			g_object_set (label,
				      "margin-left", 20,
				      "margin-right", 20,
				      "margin-top", 6,
				      "margin-bottom", 6,
				      "xalign", 1.0,
				      NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_END);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
			gtk_widget_show_all (row);
			gtk_list_box_insert (list_box,row, -1);
		}
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gs_shell_updates_activated_cb:
 **/
static void
gs_shell_updates_activated_cb (GtkListBox *list_box,
			       GtkListBoxRow *row,
			       GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GsAppWidget *app_widget;
	GsApp *app;

	app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	app = gs_app_widget_get_app (app_widget);

	g_clear_object (&priv->app);
	priv->app = g_object_ref (app);

	show_update_details (priv->app, shell_updates);
}

/**
 * gs_shell_updates_is_addon_id_kind
 **/
static gboolean
gs_shell_updates_is_addon_id_kind (GsApp *app)
{
	GsAppIdKind id_kind;
	id_kind = gs_app_get_id_kind (app);
	if (id_kind == GS_APP_ID_KIND_DESKTOP)
		return FALSE;
	if (id_kind == GS_APP_ID_KIND_WEBAPP)
		return FALSE;
	return TRUE;
}

/**
 * gs_shell_updates_list_header_func
 **/
static void
gs_shell_updates_list_header_func (GtkListBoxRow *row,
				   GtkListBoxRow *before,
				   gpointer user_data)
{
	GsAppWidget *aw1;
	GsAppWidget *aw2;
	GtkStyleContext *context;
	GtkWidget *header;

	/* first entry */
	gtk_list_box_row_set_header (row, NULL);
	if (before == NULL)
		return;

	/* desktop -> addons */
	aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (before)));
	aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	if (!gs_shell_updates_is_addon_id_kind (gs_app_widget_get_app (aw1)) &&
	    gs_shell_updates_is_addon_id_kind (gs_app_widget_get_app (aw2))) {
		/* TRANSLATORS: This is the header dividing the normal
		 * applications and the addons */
		header = gtk_label_new (_("Add-ons"));
		g_object_set (header,
			      "xalign", 0.0,
			      NULL);
		context = gtk_widget_get_style_context (header);
		gtk_style_context_add_class (context, "header-label");
	} else {
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	}
	gtk_list_box_row_set_header (row, header);
}

/**
 * gs_shell_updates_button_close_cb:
 **/
static void
gs_shell_updates_button_close_cb (GtkWidget *widget, GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_widget_hide (widget);
}

/**
 * gs_shell_updates_button_back_cb:
 **/
static void
gs_shell_updates_button_back_cb (GtkWidget *widget, GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	/* return to the list view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_update_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_window_set_title (GTK_WINDOW (widget), gs_app_get_name (priv->app));
}

/**
 * gs_shell_updates_changed_cb:
 */
static void
gs_shell_updates_changed_cb (GsPluginLoader *plugin_loader,
			     GsShellUpdates *shell_updates)
{
	gs_shell_updates_invalidate (shell_updates);
	gs_shell_updates_refresh (shell_updates, FALSE, TRUE);
}

/**
 * gs_shell_updates_pending_apps_changed_cb:
 */
static void
gs_shell_updates_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
					  GsShellUpdates *shell_updates)
{
	gs_shell_updates_invalidate (shell_updates);
}

static void
gs_shell_updates_button_update_all_cb (GtkButton      *button,
				       GsShellUpdates *updates)
{
	gs_offline_updates_trigger ();
	gs_reboot (gs_offline_updates_cancel);
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
        GtkWidget *frame;

        frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));

	if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	} else {
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
	}
}

static void
dialog_update_hide_cb (GtkWidget *dialog, GsShellUpdates *shell_updates)
{
	g_clear_object (&shell_updates->priv->app);
}


/**
 * gs_shell_installed_sort_func:
 **/
static gchar *
gs_shell_updates_get_app_sort_key (GsApp *app)
{
	GString *key;

	key = g_string_sized_new (64);

	/* sort by kind */
	switch (gs_app_get_kind (app)) {
	case GS_APP_KIND_OS_UPDATE:
		g_string_append (key, "1:");
		break;
	default:
		g_string_append (key, "2:");
		break;
	}

	/* sort desktop files, then addons */
	switch (gs_app_get_id_kind (app)) {
	case GS_APP_ID_KIND_DESKTOP:
		g_string_append (key, "1:");
		break;
	default:
		g_string_append (key, "2:");
		break;
	}

	/* sort by install date */
	g_string_append_printf (key, "%09" G_GUINT64_FORMAT ":",
				G_MAXUINT64 - gs_app_get_install_date (app));

	/* finally, sort by short name */
	g_string_append (key, gs_app_get_name (app));
	return g_string_free (key, FALSE);
}

/**
 * gs_shell_updates_sort_func:
 **/
static gint
gs_shell_updates_sort_func (GtkListBoxRow *a,
			    GtkListBoxRow *b,
			    gpointer user_data)
{
	GsAppWidget *aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (a)));
	GsAppWidget *aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (b)));
	GsApp *a1 = gs_app_widget_get_app (aw1);
	GsApp *a2 = gs_app_widget_get_app (aw2);
	gchar *key1 = gs_shell_updates_get_app_sort_key (a1);
	gchar *key2 = gs_shell_updates_get_app_sort_key (a2);
	gint retval;

	/* compare the keys according to the algorithm above */
	retval = g_strcmp0 (key1, key2);

	g_free (key1);
	g_free (key2);

	return retval;
}

void
gs_shell_updates_setup (GsShellUpdates *shell_updates,
			GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;
	GtkWidget *sw;

	g_return_if_fail (GS_IS_SHELL_UPDATES (shell_updates));

	priv->shell = shell;

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_updates_pending_apps_changed_cb),
			  shell_updates);
	g_signal_connect (priv->plugin_loader, "updates-changed",
			  G_CALLBACK (gs_shell_updates_changed_cb),
			  shell_updates);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup updates */
	priv->list_box_updates = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_updates"));
	g_signal_connect (priv->list_box_updates, "row-activated",
			  G_CALLBACK (gs_shell_updates_activated_cb), shell_updates);
	gtk_list_box_set_header_func (priv->list_box_updates,
				      gs_shell_updates_list_header_func,
				      shell_updates,
				      NULL);
	gtk_list_box_set_sort_func (priv->list_box_updates,
				    gs_shell_updates_sort_func,
				    shell_updates, NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "list_box_update"));
	g_signal_connect (GTK_LIST_BOX (widget), "row-activated",
			  G_CALLBACK (gs_shell_updates_row_activated_cb), shell_updates);
	gtk_list_box_set_header_func (GTK_LIST_BOX (widget),
				      gs_shell_updates_list_header_func,
				      shell_updates,
				      NULL);

       widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gs_shell_updates_button_update_all_cb), shell_updates);

	/* setup update details window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_updates_button_close_cb),
			  shell_updates);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	g_signal_connect (widget, "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete), shell_updates);
	g_signal_connect (widget, "hide",
			  G_CALLBACK (dialog_update_hide_cb), shell_updates);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_updates_button_back_cb),
			  shell_updates);
	sw = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update_details"));
	widget = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (sw));
	g_signal_connect (widget, "map", G_CALLBACK (scrollbar_mapped_cb), sw);
	g_signal_connect (widget, "unmap", G_CALLBACK (scrollbar_mapped_cb), sw);

	sw = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	widget = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (sw));
	g_signal_connect (widget, "map", G_CALLBACK (scrollbar_mapped_cb), sw);
	g_signal_connect (widget, "unmap", G_CALLBACK (scrollbar_mapped_cb), sw);
}

/**
 * gs_shell_updates_class_init:
 **/
static void
gs_shell_updates_class_init (GsShellUpdatesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_updates_finalize;

	g_type_class_add_private (klass, sizeof (GsShellUpdatesPrivate));
}

/**
 * gs_shell_updates_init:
 **/
static void
gs_shell_updates_init (GsShellUpdates *shell_updates)
{
	shell_updates->priv = GS_SHELL_UPDATES_GET_PRIVATE (shell_updates);
}

/**
 * gs_shell_updates_finalize:
 **/
static void
gs_shell_updates_finalize (GObject *object)
{
	GsShellUpdates *shell_updates = GS_SHELL_UPDATES (object);
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);
	g_clear_object (&priv->app);

	G_OBJECT_CLASS (gs_shell_updates_parent_class)->finalize (object);
}

/**
 * gs_shell_updates_new:
 **/
GsShellUpdates *
gs_shell_updates_new (void)
{
	GsShellUpdates *shell_updates;
	shell_updates = g_object_new (GS_TYPE_SHELL_UPDATES, NULL);
	return GS_SHELL_UPDATES (shell_updates);
}

/* vim: set noexpandtab: */
