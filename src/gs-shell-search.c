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

#include <string.h>
#include <glib/gi18n.h>

#include "gs-shell-search.h"
#include "gs-shell.h"
#include "gs-app.h"
#include "gs-utils.h"
#include "gs-app-widget.h"

static void	gs_shell_search_finalize	(GObject	*object);

#define GS_SHELL_SEARCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_SEARCH, GsShellSearchPrivate))

struct GsShellSearchPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GtkListBox		*list_box_search;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	gboolean		 waiting;
	GsShell			*shell;
	gchar			*value;
};

G_DEFINE_TYPE (GsShellSearch, gs_shell_search, G_TYPE_OBJECT)

static void
gs_shell_search_app_widget_activated_cb (GtkListBox *list_box,
					 GtkListBoxRow *row,
					 GsShellSearch *shell_search)
{
	GsApp *app;
	GsAppWidget *app_widget;

	app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	app = gs_app_widget_get_app (app_widget);
	gs_shell_show_app (shell_search->priv->shell, app);
}

typedef struct {
	GsApp			*app;
	GsShellSearch		*shell_search;
} GsShellSearchHelper;

static void
gs_shell_search_app_installed_cb (GObject *source,
				  GAsyncResult *res,
				  gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellSearchHelper *helper = (GsShellSearchHelper *) user_data;
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to install %s: %s",
			   gs_app_get_id (helper->app),
			   error->message);
		g_error_free (error);
	} else {
		gs_app_notify_installed (helper->app);
	}
	g_object_unref (helper->app);
	g_object_unref (helper->shell_search);
	g_free (helper);
}

/**
 * gs_shell_search_finished_func:
 **/
static void
gs_shell_search_app_removed_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to remove: %s",
			   error->message);
		g_error_free (error);
	}
}

/**
 * gs_shell_search_app_remove:
 **/
static void
gs_shell_search_app_remove (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GString *markup;
	GtkResponseType response;
	GtkWidget *dialog;
	GtkWindow *window;

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	markup = g_string_new ("");
	g_string_append_printf (markup,
				/* TRANSLATORS: this is a prompt message, and
				 * '%s' is an application summary, e.g. 'GNOME Clocks' */
				_("Are you sure you want to remove %s?"),
				gs_app_get_name (app));
	g_string_prepend (markup, "<b>");
	g_string_append (markup, "</b>");
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup->str);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
						    /* TRANSLATORS: longer dialog text */
						    _("%s will be removed, and you will have to install it to use it again."),
						    gs_app_get_name (app));
	/* TRANSLATORS: this is button text to remove the application */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		g_debug ("remove %s", gs_app_get_id (app));
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   app,
						   GS_PLUGIN_LOADER_ACTION_REMOVE,
						   priv->cancellable,
						   gs_shell_search_app_removed_cb,
						   shell_search);
	}
	g_string_free (markup, TRUE);
	gtk_widget_destroy (dialog);
}

/**
 * gs_shell_search_app_install:
 **/
static void
gs_shell_search_app_install (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GsShellSearchHelper *helper;
	helper = g_new0 (GsShellSearchHelper, 1);
	helper->app = g_object_ref (app);
	helper->shell_search = g_object_ref (shell_search);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   priv->cancellable,
					   gs_shell_search_app_installed_cb,
					   helper);
}

/**
 * gs_shell_search_app_widget_clicked_cb:
 **/
static void
gs_shell_search_app_widget_clicked_cb (GsAppWidget *app_widget,
				       GsShellSearch *shell_search)
{
	GsApp *app;
	app = gs_app_widget_get_app (app_widget);
	if (gs_app_get_state (app) == GS_APP_STATE_AVAILABLE)
		gs_shell_search_app_install (shell_search, app);
	else if (gs_app_get_state (app) == GS_APP_STATE_INSTALLED)
		gs_shell_search_app_remove (shell_search, app);
}

/**
 * gs_shell_search_get_search_cb:
 **/
static void
gs_shell_search_get_search_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsShellSearch *shell_search = GS_SHELL_SEARCH (user_data);
	GsShellSearchPrivate *priv = shell_search->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_search"));
	gs_stop_spinner (GTK_SPINNER (widget));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_search"));

	priv->waiting = FALSE;

	list = gs_plugin_loader_search_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get search apps: %s", error->message);
		g_error_free (error);
		gtk_stack_set_visible_child_name (GTK_STACK (widget), "no-results");
		goto out;
	}
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "results");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		widget = gs_app_widget_new ();
		g_signal_connect (widget, "button-clicked",
				  G_CALLBACK (gs_shell_search_app_widget_clicked_cb),
				  shell_search);
		gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_search), widget);
		gs_app_widget_set_size_groups (GS_APP_WIDGET (widget),
					       priv->sizegroup_image,
					       priv->sizegroup_name);
		gtk_widget_show (widget);
	}

out: ;
}

/**
 * gs_shell_search_refresh:
 **/
void
gs_shell_search_refresh (GsShellSearch *shell_search, const gchar *value, gboolean scroll_up)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkWidget *widget;
	GtkSpinner *spinner;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_widget_show (widget);

	if (priv->waiting)
		return;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_search"));
        if (scroll_up) {
                GtkAdjustment *adj;
                adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (widget));
                gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
        }
        if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_SEARCH) {
                gs_grab_focus_when_mapped (widget);
        }

	if (g_strcmp0 (value, priv->value) == 0)
		return;

	g_free (priv->value);
	priv->value = g_strdup (value);

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (priv->list_box_search));

	/* search for apps */
	gs_plugin_loader_search_async (priv->plugin_loader,
				       value,
				       GS_PLUGIN_REFINE_FLAGS_DEFAULT |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
				       priv->cancellable,
				       gs_shell_search_get_search_cb,
				       shell_search);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_search"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "spinner");
	spinner = GTK_SPINNER (gtk_builder_get_object (priv->builder, "spinner_search"));
	gs_start_spinner (spinner);
	priv->waiting = TRUE;
}

/**
 * gs_shell_search_filter_text_changed_cb:
 **/
static void
gs_shell_search_filter_text_changed_cb (GtkEntry *entry,
					GsShellSearch *shell_search)
{
	/* FIXME: do something? */
}

/**
 * gs_shell_installed_sort_func:
 *
 * Get a sort key to achive this:
 *
 * 1. Application rating
 * 2. Length of the long description
 * 3. Number of screenshots
 * 4. Install date
 * 5. Name
 **/
static gchar *
gs_shell_search_get_app_sort_key (GsApp *app)
{
	GPtrArray *ss;
	GString *key;
	const gchar *desc;
	gint rating;

	/* sort installed, removing, other */
	key = g_string_sized_new (64);

	/* artificially cut the rating of applications with no description */
	desc = gs_app_get_description (app);
	g_string_append_printf (key, "%c:", desc != NULL ? '2' : '1');

	/* sort by rating */
	rating = gs_app_get_rating (app);
	g_string_append_printf (key, "%03i:", rating > 0 ? rating : 0);

	/* sort by length of description */
	g_string_append_printf (key, "%03" G_GSIZE_FORMAT ":",
				desc != NULL ? strlen (desc) : 0);

	/* sort by number of screenshots */
	ss = gs_app_get_screenshots (app);
	g_string_append_printf (key, "%02i:", ss->len);

	/* sort by install date */
	g_string_append_printf (key, "%09" G_GUINT64_FORMAT ":",
				G_MAXUINT64 - gs_app_get_install_date (app));

	/* finally, sort by short name */
	g_string_append (key, gs_app_get_name (app));

	return g_string_free (key, FALSE);
}

/**
 * gs_shell_search_sort_func:
 **/
static gint
gs_shell_search_sort_func (GtkListBoxRow *a,
			   GtkListBoxRow *b,
			   gpointer user_data)
{
	GsAppWidget *aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (a)));
	GsAppWidget *aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (b)));
	GsApp *a1 = gs_app_widget_get_app (aw1);
	GsApp *a2 = gs_app_widget_get_app (aw2);
	gchar *key1 = gs_shell_search_get_app_sort_key (a1);
	gchar *key2 = gs_shell_search_get_app_sort_key (a2);
	gint retval;

	/* compare the keys according to the algorithm above */
	retval = g_strcmp0 (key2, key1);

	g_free (key1);
	g_free (key2);

	return retval;
}

/**
 * gs_shell_search_list_header_func
 **/
static void
gs_shell_search_list_header_func (GtkListBoxRow *row,
				     GtkListBoxRow *before,
				     gpointer user_data)
{
	GtkWidget *header;

	/* first entry */
	header = gtk_list_box_row_get_header (row);
	if (before == NULL) {
		gtk_list_box_row_set_header (row, NULL);
		return;
	}

	/* already set */
	if (header != NULL)
		return;

	/* set new */
	header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

/**
 * gs_shell_search_setup:
 */
void
gs_shell_search_setup (GsShellSearch *shell_search,
		       GsShell *shell,
			  GsPluginLoader *plugin_loader,
			  GtkBuilder *builder,
			  GCancellable *cancellable)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_SEARCH (shell_search));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);
	priv->shell = shell;

	/* refilter on search box changing */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "search-changed",
			  G_CALLBACK (gs_shell_search_filter_text_changed_cb), shell_search);

	/* setup search */
	priv->list_box_search = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_search"));
	g_signal_connect (priv->list_box_search, "row-activated",
			  G_CALLBACK (gs_shell_search_app_widget_activated_cb), shell_search);
	gtk_list_box_set_header_func (priv->list_box_search,
				      gs_shell_search_list_header_func,
				      shell_search, NULL);
	gtk_list_box_set_sort_func (priv->list_box_search,
				    gs_shell_search_sort_func,
				    shell_search, NULL);
}

/**
 * gs_shell_search_class_init:
 **/
static void
gs_shell_search_class_init (GsShellSearchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_search_finalize;

	g_type_class_add_private (klass, sizeof (GsShellSearchPrivate));
}

/**
 * gs_shell_search_init:
 **/
static void
gs_shell_search_init (GsShellSearch *shell_search)
{
	shell_search->priv = GS_SHELL_SEARCH_GET_PRIVATE (shell_search);
	shell_search->priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	shell_search->priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

/**
 * gs_shell_search_finalize:
 **/
static void
gs_shell_search_finalize (GObject *object)
{
	GsShellSearch *shell_search = GS_SHELL_SEARCH (object);
	GsShellSearchPrivate *priv = shell_search->priv;

	g_object_unref (priv->sizegroup_image);
	g_object_unref (priv->sizegroup_name);

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);
	g_free (priv->value);

	G_OBJECT_CLASS (gs_shell_search_parent_class)->finalize (object);
}

/**
 * gs_shell_search_new:
 **/
GsShellSearch *
gs_shell_search_new (void)
{
	GsShellSearch *shell_search;
	shell_search = g_object_new (GS_TYPE_SHELL_SEARCH, NULL);
	return GS_SHELL_SEARCH (shell_search);
}

/* vim: set noexpandtab: */
