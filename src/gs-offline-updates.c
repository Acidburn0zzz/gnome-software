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
#include <packagekit-glib2/packagekit.h>
#include <polkit/polkit.h>
#include <pango/pango-font.h>

#include "gs-offline-updates.h"
#include "gs-utils.h"

static void
expander_cb (GtkExpander *expander, GParamSpec *pspec, GtkWindow *dialog)
{
	gtk_window_set_resizable (dialog, gtk_expander_get_expanded (expander));
}

static void
do_not_expand (GtkWidget *child, gpointer data)
{
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (child)),
				 child, "expand", FALSE, "fill", FALSE, NULL);
}

static const gchar *
prepare_secondary_text (PkError *pk_error)
{
	g_return_val_if_fail (pk_error != NULL, NULL);

	switch (pk_error_get_code (pk_error)) {
	case PK_ERROR_ENUM_UNFINISHED_TRANSACTION:
		/* TRANSLATORS: the transaction could not be completed
 		 * as a previous transaction was unfinished */
		return _("A previous update was unfinished.");
		break;
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
	case PK_ERROR_ENUM_NO_CACHE:
	case PK_ERROR_ENUM_NO_NETWORK:
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
	case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
		/* TRANSLATORS: the package manager needed to download
		 * something with no network available */
		return _("Network access was required but not available.");
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
	case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
	case PK_ERROR_ENUM_GPG_FAILURE:
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		/* TRANSLATORS: if the package is not signed correctly
		 *  */
		return _("An update was not signed in the correct way.");
		break;
	case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
	case PK_ERROR_ENUM_FILE_CONFLICTS:
	case PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE:
	case PK_ERROR_ENUM_PACKAGE_CONFLICTS:
		/* TRANSLATORS: the transaction failed in a way the user
		 * probably cannot comprehend. Package management systems
		 * really are teh suck.*/
		return _("The update could not be completed.");
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		/* TRANSLATORS: the user aborted the update manually */
		return _("The update was cancelled.");
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
	case PK_ERROR_ENUM_UPDATE_NOT_FOUND:
		/* TRANSLATORS: the user must have updated manually after
		 * the updates were prepared */
		return _("An offline update was requested but no packages required updating.");
		break;
	case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE:
		/* TRANSLATORS: we ran out of disk space */
		return _("No space was left on the drive.");
		break;
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD:
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL:
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE:
		/* TRANSLATORS: the update process failed in a general
		 * way, usually this message will come from source distros
		 * like gentoo */
		return _("An update failed to install correctly.");
		break;
	default:
		/* TRANSLATORS: We didn't handle the error type */
		return _("The offline update failed in an unexpected way.");
		break;
	}
}

static const gchar *
prepare_details (PkError *pk_error)
{
	gboolean show_geeky;
	g_return_val_if_fail (pk_error != NULL, NULL);

	switch (pk_error_get_code (pk_error)) {
	case PK_ERROR_ENUM_UNFINISHED_TRANSACTION:
		/* A previous update was unfinished */
		show_geeky = TRUE;
		break;
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
	case PK_ERROR_ENUM_NO_CACHE:
	case PK_ERROR_ENUM_NO_NETWORK:
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
	case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
		/* Network access was required but not available */
		show_geeky = FALSE;
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
	case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
	case PK_ERROR_ENUM_GPG_FAILURE:
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		/* An update was not signed in the correct way */
		show_geeky = TRUE;
		break;
	case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
	case PK_ERROR_ENUM_FILE_CONFLICTS:
	case PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE:
	case PK_ERROR_ENUM_PACKAGE_CONFLICTS:
		/* The update could not be completed */
		show_geeky = TRUE;
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		/* The update was cancelled */
		show_geeky = FALSE;
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
	case PK_ERROR_ENUM_UPDATE_NOT_FOUND:
		/* An offline update was requested but no packages required updating */
		show_geeky = FALSE;
		break;
	case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE:
		/* No space was left on the drive */
		show_geeky = FALSE;
		break;
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD:
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL:
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE:
		/* An update failed to install correctly */
		show_geeky = TRUE;
		break;
	default:
		/* The offline update failed in an unexpected way */
		show_geeky = TRUE;
		break;
	}

	if (show_geeky)
		return pk_error_get_details (pk_error);
	else
		return NULL;
}

/**
 * insert_details_widget:
 * @dialog: the message dialog where the widget will be inserted
 * @details: (allow-none): the detailed message text to display
 *
 * Inserts a widget displaying the detailed message into the message dialog.
 * Does nothing if @details is %NULL so it is safe to call this function
 * without checking if there is anything to display.
 */
static void
insert_details_widget (GtkMessageDialog *dialog, const gchar *details)
{
	GtkWidget *message_area, *sw, *label;
	GtkWidget *box, *expander;
	PangoFontMetrics *metrics;
	int lineheight;
	GString *msg;

	if (!details)
		return;
	g_return_if_fail (dialog != NULL);

	msg = g_string_new ("");
	g_string_append_printf (msg, "%s\n\n%s",
				/* TRANSLATORS: these are geeky messages from the
				 * package manager no mortal is supposed to understand,
				 * but google might know what they mean */
				_("Detailed errors from the package manager follow:"),
				details);

	message_area = gtk_message_dialog_get_message_area (dialog);
	g_assert (GTK_IS_BOX (message_area));
	/* make the hbox expand */
	box = gtk_widget_get_parent (message_area);
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (box)), box,
				 "expand", TRUE, "fill", TRUE, NULL);
	/* make the labels not expand */
	gtk_container_foreach (GTK_CONTAINER (message_area), do_not_expand, NULL);

	/* TODO: In 3.18 the label will be "Details:" (":" appended) */
	expander = gtk_expander_new (_("Details"));
	gtk_widget_set_visible (expander, TRUE);
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_widget_set_visible (sw, TRUE);

	label = gtk_label_new (msg->str);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_line_wrap_mode (GTK_LABEL (label), GTK_WRAP_WORD);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_START);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_widget_set_visible (label, TRUE);

	/* Setup the minimum size of the text label */
	gtk_label_set_width_chars (GTK_LABEL (label), 50);
	metrics = pango_context_get_metrics (
			gtk_widget_get_pango_context (label), NULL, NULL);
	lineheight = PANGO_PIXELS_CEIL (
			pango_font_metrics_get_ascent (metrics)
			+ pango_font_metrics_get_descent (metrics));
	pango_font_metrics_unref (metrics);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW (sw),
						   5 * lineheight);

	gtk_container_add (GTK_CONTAINER (sw), label);
	gtk_container_add (GTK_CONTAINER (expander), sw);
	gtk_box_pack_end (GTK_BOX (message_area), expander, TRUE, TRUE, 0);
	g_signal_connect (expander, "notify::expanded",
			  G_CALLBACK (expander_cb), dialog);

	g_string_free (msg, TRUE);
}

void
gs_offline_updates_show_error (GsShell *shell)
{
	const gchar *title;
	const gchar *secondary;
	const gchar *geeky;
	GtkWidget *dialog;
	GError *error = NULL;
	PkError *pk_error = NULL;
	PkResults *results = NULL;

	results = pk_offline_get_results (NULL);
	if (results == NULL)
		goto out;
	pk_error = pk_results_get_error_code (results);
	if (pk_error == NULL)
		goto out;

	/* can this happen in reality? */
	if (pk_results_get_exit_code (results) == PK_EXIT_ENUM_SUCCESS)
		goto out;

	/* TRANSLATORS: this is when the offline update failed */
	title = _("Failed To Update");
	secondary = prepare_secondary_text (pk_error);
	geeky = prepare_details (pk_error);

	dialog = gtk_message_dialog_new_with_markup (gs_shell_get_window (shell),
					 0,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CLOSE,
					 "<big><b>%s</b></big>", title);
	if (secondary)
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", secondary);

	insert_details_widget (GTK_MESSAGE_DIALOG (dialog), geeky);

	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy),
				  dialog);
	gtk_widget_show (dialog);

	if (!pk_offline_clear_results (NULL, &error)) {
		g_warning ("Failure clearing offline update message: %s",
			   error->message);
		g_error_free (error);
	}
out:
	if (pk_error != NULL)
		g_object_unref (pk_error);
	if (results != NULL)
		g_object_unref (results);
}

GPermission *
gs_offline_updates_permission_get (void)
{
	static GPermission *permission;

	if (!permission)
		permission = polkit_permission_new_sync ("org.freedesktop.packagekit.trigger-offline-update",
                                                         NULL, NULL, NULL);

	return permission;
}

gboolean
gs_updates_are_managed (void)
{
	GPermission *permission;
	gboolean managed;

	permission = gs_offline_updates_permission_get ();
	managed = !g_permission_get_allowed (permission) &&
                  !g_permission_get_can_acquire (permission);

	return managed;
}


/* vim: set noexpandtab: */
