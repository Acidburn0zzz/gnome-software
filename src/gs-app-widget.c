/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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
#include "gs-utils.h"

struct _GsAppWidgetPrivate
{
	GsApp		*app;
	GtkWidget	*image;
	GtkWidget	*name_box;
	GtkWidget	*name_label;
	GtkWidget	*version_label;
	GtkWidget	*description_label;
	GtkWidget	*button_box;
	GtkWidget	*button;
	GtkWidget	*spinner;
	gboolean	 colorful;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsAppWidget, gs_app_widget, GTK_TYPE_BIN)

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
	GtkStyleContext *context;
	const gchar *tmp = NULL;
	GString *str = NULL;

	if (app_widget->priv->app == NULL)
		return;

	/* get the main body text */
	if (gs_app_get_state (priv->app) == GS_APP_STATE_UPDATABLE)
		tmp = gs_app_get_update_details (priv->app);
	if (tmp == NULL)
		tmp = gs_app_get_description (priv->app);
	if (tmp == NULL)
		tmp = gs_app_get_summary (priv->app);
	if (tmp == NULL)
		tmp = gs_app_get_name (priv->app);

	/* join the lines*/
	str = g_string_new (tmp);
	gs_string_replace (str, "\n", " ");

	gtk_label_set_label (GTK_LABEL (priv->description_label), str->str);
	g_string_free (str, TRUE);

	gtk_label_set_label (GTK_LABEL (priv->name_label),
			     gs_app_get_name (priv->app));
	if (gs_app_get_state (priv->app) == GS_APP_STATE_UPDATABLE) {
		gtk_label_set_label (GTK_LABEL (priv->version_label),
				     gs_app_get_update_version (priv->app));
	} else {
		gtk_label_set_label (GTK_LABEL (priv->version_label),
				     gs_app_get_version (priv->app));
	}
	if (gs_app_get_pixbuf (priv->app))
		gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image),
					   gs_app_get_pixbuf (priv->app));
	gtk_widget_set_visible (priv->button, FALSE);
	gtk_widget_set_sensitive (priv->button, TRUE);

	context = gtk_widget_get_style_context (priv->button);
	gtk_style_context_remove_class (context, "destructive-action");

	switch (gs_app_get_state (app_widget->priv->app)) {
	case GS_APP_STATE_AVAILABLE:
		gtk_widget_set_visible (priv->spinner, FALSE);
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be easily installed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Install"));
		break;
	case GS_APP_STATE_INSTALLED:
		gtk_widget_set_visible (priv->spinner, FALSE);
		if (gs_app_get_kind (app_widget->priv->app) != GS_APP_KIND_SYSTEM)
			gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be easily removed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Remove"));
		if (priv->colorful)
			gtk_style_context_add_class (context, "destructive-action");
		break;
	case GS_APP_STATE_UPDATABLE:
		gtk_widget_set_visible (priv->spinner, FALSE);
		gtk_widget_set_visible (priv->button, FALSE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be updated. not normally shown */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Update"));
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_spinner_start (GTK_SPINNER (priv->spinner));
		gtk_widget_set_visible (priv->spinner, TRUE);
		gtk_widget_set_visible (priv->button, TRUE);
		gtk_widget_set_sensitive (priv->button, FALSE);
		/* TRANSLATORS: this is a button next to the search results that
		 * shows the status of an application being installed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Installing"));
		break;
	case GS_APP_STATE_REMOVING:
		gtk_spinner_start (GTK_SPINNER (priv->spinner));
		gtk_widget_set_visible (priv->spinner, TRUE);
		gtk_widget_set_visible (priv->button, TRUE);
		gtk_widget_set_sensitive (priv->button, FALSE);
		/* TRANSLATORS: this is a button next to the search results that
		 * shows the status of an application being erased */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Removing"));
		break;
	default:
		break;
	}
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
	g_signal_connect_object (app_widget->priv->app, "state-changed",
				 G_CALLBACK (gs_app_widget_refresh),
				 app_widget, G_CONNECT_SWAPPED);
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

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/software/app-widget.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, image);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, name_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, version_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, description_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, button_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppWidget, spinner);
}

static void
button_clicked (GtkWidget *widget, GsAppWidget *app_widget)
{
	g_signal_emit (app_widget, signals[SIGNAL_BUTTON_CLICKED], 0);
}

static void
gs_app_widget_init (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv;

	priv = gs_app_widget_get_instance_private (app_widget);
	app_widget->priv = priv;

	gtk_widget_set_has_window (GTK_WIDGET (app_widget), FALSE);
	gtk_widget_init_template (GTK_WIDGET (app_widget));

	priv->colorful = TRUE;

	g_signal_connect (priv->button, "clicked",
			  G_CALLBACK (button_clicked), app_widget);
}

void
gs_app_widget_set_size_groups (GsAppWidget  *app_widget,
			       GtkSizeGroup *image,
			       GtkSizeGroup *name)
{
	gtk_size_group_add_widget (image, app_widget->priv->image);
	gtk_size_group_add_widget (name, app_widget->priv->name_box);
}

void
gs_app_widget_set_colorful (GsAppWidget *app_widget,
			    gboolean     colorful)
{
	app_widget->priv->colorful = colorful;
}

GtkWidget *
gs_app_widget_new (void)
{
	return g_object_new (GS_TYPE_APP_WIDGET, NULL);
}

/* vim: set noexpandtab: */
