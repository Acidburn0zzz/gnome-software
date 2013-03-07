/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
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
#include <gtk/gtk.h>

#include "gs-app-widget.h"
#include "ch-markdown.h"

struct _GsAppWidgetPrivate
{
	ChMarkdown	*markdown;
	gboolean	 expanded;
	gchar		*description;
	gchar		*description_more;
	GsApp		*app;
	gchar		*status;
	GsAppWidgetKind	 kind;
	GtkWidget	*widget_button;
	GtkWidget	*widget_description;
	GtkWidget	*widget_description_more;
	GtkWidget	*widget_image;
	GtkWidget	*widget_more;
	GtkWidget	*widget_name;
	GtkWidget	*widget_spinner;
	GtkWidget	*widget_status;
	GtkWidget	*widget_version;
};


#define	GS_APP_WIDGET_MAX_LINES_NO_EXPANDER	3

G_DEFINE_TYPE (GsAppWidget, gs_app_widget, GTK_TYPE_BOX)

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_app_widget_refresh:
 **/
static void
gs_app_widget_refresh (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv = app_widget->priv;

	if (app_widget->priv->app == NULL)
		return;

	gtk_label_set_label (GTK_LABEL (priv->widget_name),
			     gs_app_get_name (priv->app));
	gtk_label_set_markup (GTK_LABEL (priv->widget_description), priv->description);
	gtk_label_set_markup (GTK_LABEL (priv->widget_description_more), priv->description_more);
	gtk_label_set_label (GTK_LABEL (priv->widget_status), priv->status);
	gtk_label_set_label (GTK_LABEL (priv->widget_version),
			     gs_app_get_version (priv->app));
	gtk_image_set_from_pixbuf (GTK_IMAGE (priv->widget_image),
				   gs_app_get_pixbuf (priv->app));
	gtk_widget_set_visible (priv->widget_name, TRUE);
	gtk_widget_set_visible (priv->widget_description, TRUE);
	gtk_widget_set_visible (priv->widget_description_more,
				priv->expanded && priv->description_more != NULL);
	gtk_widget_set_visible (priv->widget_status, priv->status != NULL);
	gtk_widget_set_visible (priv->widget_version, TRUE);
	gtk_widget_set_visible (priv->widget_image, TRUE);
	gtk_widget_set_visible (priv->widget_button, TRUE);
	gtk_widget_set_visible (priv->widget_more,
				!priv->expanded && app_widget->priv->description_more != NULL);

	/* show / hide widgets depending on kind */
	switch (app_widget->priv->kind) {
	case GS_APP_WIDGET_KIND_INSTALL:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button),
				      _("Install"));
		break;
	case GS_APP_WIDGET_KIND_REMOVE:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button),
				      _("Remove"));
		break;
	case GS_APP_WIDGET_KIND_UPDATE:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button),
				      _("Update"));
		break;
	case GS_APP_WIDGET_KIND_BUSY:
		gtk_spinner_start (GTK_SPINNER (priv->widget_spinner));
		gtk_widget_set_visible (priv->widget_spinner, TRUE);
		gtk_widget_set_visible (priv->widget_button, FALSE);
		break;
	default:
		gtk_widget_set_visible (priv->widget_button, FALSE);
		break;
	}
}

/**
 * gs_app_widget_get_status:
 **/
const gchar *
gs_app_widget_get_status (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->status;
}

/**
 * gs_app_widget_get_kind:
 **/
GsAppWidgetKind
gs_app_widget_get_kind (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), 0);
	return app_widget->priv->kind;
}

static guint
_g_string_replace (GString *string, const gchar *search, const gchar *replace)
{
	gchar *tmp;
	guint cnt = 0;
	guint replace_len;
	guint search_len;

	search_len = strlen (search);
	replace_len = strlen (replace);

	do {
		tmp = g_strstr_len (string->str, -1, search);
		if (tmp == NULL)
			goto out;

		/* reallocate the string if required */
		if (search_len > replace_len) {
			g_string_erase (string,
					tmp - string->str,
					search_len - replace_len);
		}
		if (search_len < replace_len) {
			g_string_insert_len (string,
					     tmp - string->str,
					     search,
					     replace_len - search_len);
		}

		/* just memcmp in the new string */
		memcpy (tmp, replace, replace_len);
		cnt++;
	} while (TRUE);
out:
	return cnt;
}

/**
 * gs_app_widget_set_description:
 **/
static void
gs_app_widget_set_description (GsAppWidget *app_widget, const gchar *description)
{
	gchar **split = NULL;
	GsAppWidgetPrivate *priv = app_widget->priv;
	GString *description2;
	GString *tmp_description_more = NULL;
	GString *tmp_description = NULL;
	guint i;

	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (description != NULL);
	g_return_if_fail (description[0] != '\0');

	g_free (priv->description);
	g_free (priv->description_more);

	/* force split with bullet */
	description2 = g_string_new (description);
	_g_string_replace (description2, ". ", "\n* ");

	/* common case, no newlines at all */
	if (g_strstr_len (description, -1, "\n") == NULL) {
		priv->description = ch_markdown_parse (priv->markdown,
						       description2->str);
		priv->description_more = NULL;
		goto out;
	}

	/* split up description into extra parts */
	split = g_strsplit (description2->str, "\n", -1);
	tmp_description = g_string_new ("");
	tmp_description_more = g_string_new ("");
	for (i = 0; split[i] != NULL; i++) {
		if (i <= GS_APP_WIDGET_MAX_LINES_NO_EXPANDER) {
			g_string_append_printf (tmp_description,
						"%s\n", split[i]);
		} else {
			g_string_append_printf (tmp_description_more,
						"%s\n", split[i]);
		}
	}

	/* remove trailing newline */
	if (tmp_description->len > 0) {
		g_string_set_size (tmp_description,
				   tmp_description->len - 1);
	}
	if (tmp_description_more->len > 0) {
		g_string_set_size (tmp_description_more,
				   tmp_description_more->len - 1);
	}

	/* parse markdown */
	priv->description = ch_markdown_parse (priv->markdown,
					       tmp_description->str);
	if (tmp_description_more->len > 0) {
		priv->description_more = ch_markdown_parse (priv->markdown,
							    tmp_description_more->str);
	} else {
		priv->description_more = NULL;
	}
out:
	g_string_free (description2, TRUE);
	if (tmp_description != NULL)
		g_string_free (tmp_description, TRUE);
	if (tmp_description_more != NULL)
		g_string_free (tmp_description_more, TRUE);
	g_strfreev (split);
}

/**
 * gs_app_widget_get_app:
 **/
GsApp *
gs_app_widget_get_app (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->app;
}

/**
 * gs_app_widget_set_app:
 **/
void
gs_app_widget_set_app (GsAppWidget *app_widget, GsApp *app)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (GS_IS_APP (app));
	app_widget->priv->app = g_object_ref (app);
	gs_app_widget_set_description (app_widget, gs_app_get_summary (app));
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_status:
 **/
void
gs_app_widget_set_status (GsAppWidget *app_widget, const gchar *status)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (status != NULL);
	g_free (app_widget->priv->status);
	app_widget->priv->status = g_strdup (status);
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_kind:
 **/
void
gs_app_widget_set_kind (GsAppWidget *app_widget, GsAppWidgetKind kind)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	app_widget->priv->kind = kind;
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_destroy:
 **/
static void
gs_app_widget_destroy (GtkWidget *object)
{
	GsAppWidget *app_widget = GS_APP_WIDGET (object);
	GsAppWidgetPrivate *priv = app_widget->priv;

	g_free (priv->description_more);
	priv->description_more = NULL;
	g_free (priv->description);
	priv->description = NULL;
	g_free (priv->status);
	priv->status = NULL;
	if (priv->markdown != NULL)
		g_clear_object (&priv->markdown);
	if (priv->app != NULL)
		g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_app_widget_parent_class)->destroy (object);
}

static void
gs_app_widget_class_init (GsAppWidgetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->destroy = gs_app_widget_destroy;

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppWidgetClass, button_clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (GsAppWidgetPrivate));
}

/**
 * gs_app_widget_button_clicked_cb:
 **/
static void
gs_app_widget_button_clicked_cb (GtkWidget *widget, GsAppWidget *app_widget)
{
	g_signal_emit (app_widget, signals[SIGNAL_BUTTON_CLICKED], 0);
}

/**
 * gs_app_widget_more_clicked_cb:
 **/
static void
gs_app_widget_more_clicked_cb (GtkWidget *widget, GsAppWidget *app_widget)
{
	app_widget->priv->expanded = TRUE;
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_init:
 **/
static void
gs_app_widget_init (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv;
	GtkStyleContext *context;
	GtkWidget *box;
	PangoAttrList *attr_list;

	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	app_widget->priv = G_TYPE_INSTANCE_GET_PRIVATE (app_widget,
							GS_TYPE_APP_WIDGET,
							GsAppWidgetPrivate);
	priv = app_widget->priv;
	priv->markdown = ch_markdown_new ();

	/* set defaults */
	gtk_box_set_spacing (GTK_BOX (app_widget), 3);
	gtk_widget_set_margin_left (GTK_WIDGET (app_widget), 9);
	gtk_widget_set_margin_top (GTK_WIDGET (app_widget), 9);
	gtk_widget_set_margin_bottom (GTK_WIDGET (app_widget), 9);

	/* pixbuf */
	priv->widget_image = gtk_image_new_from_icon_name ("edit-paste",
							   GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_margin_right (GTK_WIDGET (priv->widget_image), 9);
	gtk_widget_set_valign (priv->widget_image, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (app_widget),
			    GTK_WIDGET (priv->widget_image),
			    FALSE, FALSE, 0);

	/* name > version */
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_visible (box, TRUE);
	priv->widget_name = gtk_label_new ("name");
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_name),
				 PANGO_ELLIPSIZE_NONE);
	gtk_label_set_line_wrap (GTK_LABEL (priv->widget_name), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (priv->widget_name), 20);
	gtk_misc_set_alignment (GTK_MISC (priv->widget_name), 0.0, 0.5);
	gtk_widget_set_size_request (priv->widget_name, 200, -1);
	attr_list = pango_attr_list_new ();
	pango_attr_list_insert (attr_list,
				pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (priv->widget_name), attr_list);
	pango_attr_list_unref (attr_list);
	priv->widget_version = gtk_label_new ("version");
	gtk_misc_set_alignment (GTK_MISC (priv->widget_version), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_name),
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_version),
			    FALSE, FALSE, 12);
	gtk_box_pack_start (GTK_BOX (app_widget),
			    GTK_WIDGET (box),
			    FALSE, TRUE, 0);

	/* description */
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_visible (box, TRUE);
	priv->widget_description = gtk_label_new ("description");
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description), 0.0, 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (priv->widget_description), TRUE);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_description),
			    TRUE, TRUE, 0);

	/* 'More' Expander */
	priv->widget_more = gtk_button_new_with_label (_("More  ▾"));
	context = gtk_widget_get_style_context (priv->widget_more);
	gtk_style_context_add_class (context, "dim-label");
	gtk_button_set_relief (GTK_BUTTON (priv->widget_more), GTK_RELIEF_NONE);
	gtk_widget_set_margin_right (priv->widget_more, 36);
	gtk_widget_set_halign (priv->widget_more, GTK_ALIGN_END);
	gtk_box_pack_start (GTK_BOX (box), priv->widget_more, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (app_widget),
			    GTK_WIDGET (box),
			    TRUE, TRUE, 0);
	g_signal_connect (priv->widget_more, "clicked",
			  G_CALLBACK (gs_app_widget_more_clicked_cb), app_widget);

	/* description - more */
	priv->widget_description_more = gtk_label_new ("description-more");
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description_more), 0.0, 0.0);
	gtk_label_set_line_wrap (GTK_LABEL (priv->widget_description_more), TRUE);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_description_more),
			    TRUE, TRUE, 0);

	/* button */
	priv->widget_button = gtk_button_new_with_label ("button");
	gtk_widget_set_margin_right (GTK_WIDGET (priv->widget_button), 9);
	gtk_widget_set_size_request (priv->widget_button, 100, -1);
	gtk_widget_set_vexpand (priv->widget_button, FALSE);
	gtk_widget_set_hexpand (priv->widget_button, FALSE);
	gtk_widget_set_halign (priv->widget_button, GTK_ALIGN_END);
	g_signal_connect (priv->widget_button, "clicked",
			  G_CALLBACK (gs_app_widget_button_clicked_cb), app_widget);

	/* spinner */
	priv->widget_spinner = gtk_spinner_new ();
	gtk_widget_set_margin_right (GTK_WIDGET (priv->widget_spinner), 18);
	gtk_widget_set_size_request (priv->widget_spinner, 48, 48);

	/* status */
	priv->widget_status = gtk_label_new ("status");
	gtk_misc_set_alignment (GTK_MISC (priv->widget_status), 1.0, 0.0);
	context = gtk_widget_get_style_context (priv->widget_status);
	gtk_style_context_add_class (context, "dim-label");
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_status),
				 PANGO_ELLIPSIZE_NONE);
	gtk_label_set_line_wrap (GTK_LABEL (priv->widget_status), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (priv->widget_status), 20);
	gtk_widget_set_margin_right (GTK_WIDGET (priv->widget_status), 9);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	gtk_widget_set_visible (box, TRUE);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_button),
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_spinner),
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_status),
			    FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (app_widget),
			    GTK_WIDGET (box),
			    FALSE, FALSE, 0);

	/* refresh */
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_new:
 **/
GtkWidget *
gs_app_widget_new (void)
{
	return g_object_new (GS_TYPE_APP_WIDGET, NULL);
}

