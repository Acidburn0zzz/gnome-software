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

#include <glib.h>
#include <string.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "appstream-common.h"
#include "appstream-markup.h"

#include "gs-moduleset.h"

static void
appstream_common_func (void)
{
	gchar *tmp;

	g_assert_cmpstr (appstream_tag_to_string (APPSTREAM_TAG_LICENCE), == ,"licence");
	g_assert_cmpint (appstream_tag_from_string ("licence"), == ,APPSTREAM_TAG_LICENCE);
	g_assert_cmpint (appstream_get_locale_value ("C"), ==, G_MAXUINT - 1);
	g_assert_cmpint (appstream_get_locale_value ("xxx"), ==, G_MAXUINT);

	/* test unmunging white-space */
	tmp = appstream_xml_unmunge ("  This is a sample.\n\nData was collected.  ", -1);
	g_assert_cmpstr (tmp, ==, "This is a sample. Data was collected.");
	g_free (tmp);

	/* test unmunging escape chars */
	tmp = appstream_xml_unmunge ("Bar &amp; &#34;Nob&#34; &gt; &#39;eBay&#39;", -1);
	g_assert_cmpstr (tmp, ==, "Bar & \"Nob\" > 'eBay'");
	g_free (tmp);
}

static void
appstream_markup_plain_func (void)
{
	AppstreamMarkup *markup;
	const gchar *tmp;

	markup = appstream_markup_new ();
	appstream_markup_set_enabled (markup, TRUE);
	appstream_markup_set_lang (markup, NULL);

	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_START);
	appstream_markup_add_content (markup, "This is preformatted\n\nOne", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_END);

	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_START);
	appstream_markup_set_lang (markup, "xx_XX");
	appstream_markup_add_content (markup, "This is dave\n\nOne", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_END);

	tmp = appstream_markup_get_text (markup);
	g_assert_cmpstr (tmp, ==, "This is preformatted\n\nOne");

	appstream_markup_free (markup);
}

static void
appstream_markup_tags_func (void)
{
	AppstreamMarkup *markup;
	const gchar *tmp;

	markup = appstream_markup_new ();
	appstream_markup_set_enabled (markup, TRUE);
	appstream_markup_set_lang (markup, NULL);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_START);
	appstream_markup_add_content (markup, "    ", -1);

	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_START);
	appstream_markup_add_content (markup, "Para1", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_END);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_UL_START);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_LI_START);
	appstream_markup_add_content (markup, "Item1", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_LI_END);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_LI_START);
	appstream_markup_add_content (markup, "Item2", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_LI_END);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_UL_END);

	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_END);
	tmp = appstream_markup_get_text (markup);
	g_assert_cmpstr (tmp, ==, "Para1\n • Item1\n • Item2");

	appstream_markup_free (markup);
}

static void
appstream_markup_locale_func (void)
{
	AppstreamMarkup *markup;
	const gchar *tmp;

	markup = appstream_markup_new ();
	appstream_markup_set_enabled (markup, TRUE);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_START);

	appstream_markup_set_lang (markup, "XXX");
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_START);
	appstream_markup_add_content (markup, "Para:XXX", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_END);

	appstream_markup_set_lang (markup, NULL);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_START);
	appstream_markup_add_content (markup, "Para:C", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_END);

	appstream_markup_set_lang (markup, "YYY");
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_START);
	appstream_markup_add_content (markup, "Para:YYY", -1);
	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_P_END);

	appstream_markup_set_mode (markup, APPSTREAM_MARKUP_MODE_END);
	tmp = appstream_markup_get_text (markup);
	g_assert_cmpstr (tmp, ==, "Para:C");

	appstream_markup_free (markup);
}

static void
moduleset_func (void)
{
	gboolean ret;
	gchar **data;
	GError *error = NULL;
	GsModuleset *ms;

	ms = gs_moduleset_new ();
	ret = gs_moduleset_parse_filename (ms, "./moduleset-test.xml", &error);
	g_assert_no_error (error);
	g_assert (ret);

	data = gs_moduleset_get_by_kind (ms, GS_MODULESET_MODULE_KIND_PACKAGE);
	g_assert (data != NULL);
	g_assert_cmpint (g_strv_length (data), ==, 1);
	g_assert_cmpstr (data[0], ==, "kernel");
	g_assert_cmpstr (data[1], ==, NULL);

	data = gs_moduleset_get_by_kind (ms, GS_MODULESET_MODULE_KIND_APPLICATION);
	g_assert (data != NULL);
	g_assert_cmpint (g_strv_length (data), ==, 1);
	g_assert_cmpstr (data[0], ==, "gnome-shell.desktop");
	g_assert_cmpstr (data[1], ==, NULL);

	g_object_unref (ms);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/moduleset", moduleset_func);
	g_test_add_func ("/appstream-common", appstream_common_func);
	g_test_add_func ("/appstream-markup{plain}", appstream_markup_plain_func);
	g_test_add_func ("/appstream-markup{tags}", appstream_markup_tags_func);
	g_test_add_func ("/appstream-markup{locale}", appstream_markup_locale_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
