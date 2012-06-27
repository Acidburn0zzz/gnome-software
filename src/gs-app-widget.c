/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

struct _GsAppWidgetPrivate
{
	gchar		*id;
	gchar		*name;
	gchar		*description;
	gchar		*version;
	GdkPixbuf	*pixbuf;
	GsAppWidgetKind	 kind;
	GtkWidget	*widget_name;
	GtkWidget	*widget_description;
	GtkWidget	*widget_version;
	GtkWidget	*widget_image;
	GtkWidget	*widget_button;
};

G_DEFINE_TYPE (GsAppWidget, gs_app_widget, GTK_TYPE_BOX)

/**
 * gs_app_widget_refresh:
 **/
static void
gs_app_widget_refresh (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv = app_widget->priv;

	gtk_label_set_label (GTK_LABEL (priv->widget_name), priv->name);
	gtk_label_set_label (GTK_LABEL (priv->widget_description), priv->description);
	gtk_label_set_label (GTK_LABEL (priv->widget_version), priv->version);
	gtk_image_set_from_pixbuf (GTK_IMAGE (priv->widget_image), priv->pixbuf);

	if (app_widget->priv->kind == GS_APP_WIDGET_KIND_INSTALL) {
		gtk_button_set_label (GTK_BUTTON (priv->widget_button),
				      _("Install"));
	} else if (app_widget->priv->kind == GS_APP_WIDGET_KIND_REMOVE) {
		gtk_button_set_label (GTK_BUTTON (priv->widget_button),
				      _("Remove"));
	} else {
		gtk_button_set_label (GTK_BUTTON (priv->widget_button),
				      _("Update"));
	} 
}

/**
 * gs_app_widget_get_id:
 **/
const gchar *
gs_app_widget_get_id (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->id;
}
/**
 * gs_app_widget_get_name:
 **/
const gchar *
gs_app_widget_get_name (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->name;
}

/**
 * gs_app_widget_get_version:
 **/
const gchar *
gs_app_widget_get_version (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->version;
}

/**
 * gs_app_widget_get_description:
 **/
const gchar *
gs_app_widget_get_description (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->description;
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

/**
 * gs_app_widget_set_id:
 **/
void
gs_app_widget_set_id (GsAppWidget *app_widget, const gchar *id)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (id != NULL);
	g_free (app_widget->priv->id);
	app_widget->priv->id = g_strdup (id);
}

/**
 * gs_app_widget_set_name:
 **/
void
gs_app_widget_set_name (GsAppWidget *app_widget, const gchar *name)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (name != NULL);
	g_free (app_widget->priv->name);
	app_widget->priv->name = g_strdup (name);
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_version:
 **/
void
gs_app_widget_set_version (GsAppWidget *app_widget, const gchar *version)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (version != NULL);
	g_free (app_widget->priv->version);
	app_widget->priv->version = g_strdup (version);
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_description:
 **/
void
gs_app_widget_set_description (GsAppWidget *app_widget, const gchar *description)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (description != NULL);
	g_free (app_widget->priv->description);
	app_widget->priv->description = g_strdup (description);
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_pixbuf:
 **/
void
gs_app_widget_set_pixbuf (GsAppWidget *app_widget, GdkPixbuf *pixbuf)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	if (app_widget->priv->pixbuf != NULL) {
		g_object_unref (app_widget->priv->pixbuf);
		app_widget->priv->pixbuf = NULL;
	}
	if (pixbuf != NULL)
		app_widget->priv->pixbuf = g_object_ref (pixbuf);
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

	g_free (priv->id);
	priv->id = NULL;

	GTK_WIDGET_CLASS (gs_app_widget_parent_class)->destroy (object);
}

static void
gs_app_widget_class_init (GsAppWidgetClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	widget_class->destroy = gs_app_widget_destroy;
	g_type_class_add_private (class, sizeof (GsAppWidgetPrivate));
}

/**
 * gs_app_widget_init:
 **/
static void
gs_app_widget_init (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv;
	GtkWidget *box;
	PangoAttrList *attr_list;

	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	app_widget->priv = G_TYPE_INSTANCE_GET_PRIVATE (app_widget,
							GS_TYPE_APP_WIDGET,
							GsAppWidgetPrivate);
	priv = app_widget->priv;

	/* set defaults */
	gtk_box_set_spacing (GTK_BOX (app_widget), 3);
	gtk_widget_set_margin_left (GTK_WIDGET (app_widget), 9);
	gtk_widget_set_margin_top (GTK_WIDGET (app_widget), 9);
	gtk_widget_set_margin_bottom (GTK_WIDGET (app_widget), 9);

	/* pixbuf */
	priv->widget_image = gtk_image_new_from_icon_name ("edit-paste",
							   GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_margin_right (GTK_WIDGET (priv->widget_image), 9);
	gtk_box_pack_start (GTK_BOX (app_widget),
			    GTK_WIDGET (priv->widget_image),
			    FALSE, FALSE, 0);

	/* name > version */
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
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
	priv->widget_description = gtk_label_new ("description");
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (app_widget),
			    GTK_WIDGET (priv->widget_description),
			    TRUE, TRUE, 0);

	/* button */
	priv->widget_button = gtk_button_new_with_label ("button");
	gtk_widget_set_margin_right (GTK_WIDGET (priv->widget_button), 9);
	gtk_widget_set_size_request (priv->widget_button, 100, -1);
	gtk_widget_set_vexpand (priv->widget_button, FALSE);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (box),
			    GTK_WIDGET (priv->widget_button),
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

