/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>

#include "appstream-cache.h"
#include "appstream-common.h"
#include "appstream-image.h"
#include "appstream-markup.h"
#include "appstream-screenshot.h"

static void	appstream_cache_finalize	(GObject	*object);

#define APPSTREAM_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), APPSTREAM_TYPE_CACHE, AppstreamCachePrivate))

struct AppstreamCachePrivate
{
	GPtrArray		*array;		/* of AppstreamApp */
	GPtrArray		*icon_path_array;
	GHashTable		*hash_id;	/* of AppstreamApp{id} */
	GHashTable		*hash_pkgname;	/* of AppstreamApp{pkgname} */
};

G_DEFINE_TYPE (AppstreamCache, appstream_cache, G_TYPE_OBJECT)

/**
 * appstream_cache_error_quark:
 **/
GQuark
appstream_cache_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("appstream_cache_error");
	return quark;
}

/**
 * appstream_cache_get_size:
 */
guint
appstream_cache_get_size (AppstreamCache *cache)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), 0);
	return cache->priv->array->len;
}

/**
 * appstream_cache_get_items:
 */
GPtrArray *
appstream_cache_get_items (AppstreamCache *cache)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), NULL);
	return cache->priv->array;
}

/**
 * appstream_cache_get_item_by_id:
 */
AppstreamApp *
appstream_cache_get_item_by_id (AppstreamCache *cache, const gchar *id)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), NULL);
	return g_hash_table_lookup (cache->priv->hash_id, id);
}

/**
 * appstream_cache_get_item_by_pkgname:
 */
AppstreamApp *
appstream_cache_get_item_by_pkgname (AppstreamCache *cache, const gchar *pkgname)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), NULL);
	return g_hash_table_lookup (cache->priv->hash_pkgname, pkgname);
}

/**
 * appstream_cache_icon_kind_from_string:
 */
static AppstreamAppIconKind
appstream_cache_icon_kind_from_string (const gchar *kind_str)
{
	if (g_strcmp0 (kind_str, "stock") == 0)
		return APPSTREAM_APP_ICON_KIND_STOCK;
	if (g_strcmp0 (kind_str, "remote") == 0)
		return APPSTREAM_APP_ICON_KIND_REMOTE;
	if (g_strcmp0 (kind_str, "local") == 0 ||
	    g_strcmp0 (kind_str, "cached") == 0)
		return APPSTREAM_APP_ICON_KIND_CACHED;
	return APPSTREAM_APP_ICON_KIND_UNKNOWN;
}

typedef struct {
	const gchar		*path_icons;
	AppstreamApp		*item_temp;
	gchar			*lang_temp;
	gchar			*url_type_temp;
	AppstreamCache		*cache;
	AppstreamTag		 tag;
	AppstreamTag		 tag_last_known;
	AppstreamImage		*image;
	AppstreamScreenshot	*screenshot;
	gint			 priority;
	AppstreamMarkup		*markup;
} AppstreamCacheHelper;

/**
 * appstream_cache_app_id_kind_from_string:
 */
static AppstreamAppIdKind
appstream_cache_app_id_kind_from_string (const gchar *id_kind)
{
	if (g_strcmp0 (id_kind, "desktop") == 0)
		return APPSTREAM_APP_ID_KIND_DESKTOP;
	if (g_strcmp0 (id_kind, "inputmethod") == 0)
		return APPSTREAM_APP_ID_KIND_INPUT_METHOD;
	if (g_strcmp0 (id_kind, "font") == 0)
		return APPSTREAM_APP_ID_KIND_FONT;
	if (g_strcmp0 (id_kind, "codec") == 0)
		return APPSTREAM_APP_ID_KIND_CODEC;
	if (g_strcmp0 (id_kind, "webapp") == 0)
		return APPSTREAM_APP_ID_KIND_WEBAPP;
	if (g_strcmp0 (id_kind, "source") == 0)
		return APPSTREAM_APP_ID_KIND_SOURCE;
	return APPSTREAM_APP_ID_KIND_UNKNOWN;
}

/**
 * appstream_cache_start_element_cb:
 */
static void
appstream_cache_start_element_cb (GMarkupParseContext *context,
				  const gchar *element_name,
				  const gchar **attribute_names,
				  const gchar **attribute_values,
				  gpointer user_data,
				  GError **error)
{
	AppstreamAppIconKind icon_kind;
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	AppstreamTag section_new;
	const gchar *tmp = NULL;
	gboolean set_lang = FALSE;
	guint height = 0;
	guint i;
	guint width = 0;

	/* description markup */
	if (helper->tag == APPSTREAM_TAG_DESCRIPTION) {
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "xml:lang") == 0) {
				appstream_markup_set_lang (helper->markup,
							   attribute_values[i]);
				break;
			}
		}
		if (g_strcmp0 (element_name, "p") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_P_START);
		} else if (g_strcmp0 (element_name, "ul") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_UL_START);
		} else if (g_strcmp0 (element_name, "li") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_LI_START);
		}
		return;
	}

	/* process tag start */
	section_new = appstream_tag_from_string (element_name);
	switch (section_new) {
	case APPSTREAM_TAG_APPLICATIONS:
	case APPSTREAM_TAG_APPCATEGORIES:
	case APPSTREAM_TAG_APPCATEGORY:
	case APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP:
	case APPSTREAM_TAG_KEYWORDS:
	case APPSTREAM_TAG_KEYWORD:
	case APPSTREAM_TAG_MIMETYPES:
	case APPSTREAM_TAG_METADATA:
	case APPSTREAM_TAG_MIMETYPE:
	case APPSTREAM_TAG_PRIORITY:
	case APPSTREAM_TAG_LANGUAGES:
		/* ignore */
		break;
	case APPSTREAM_TAG_ID:
		if (helper->item_temp == NULL ||
		    helper->tag != APPSTREAM_TAG_APPLICATION) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get the id kind */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				tmp = attribute_values[i];
				break;
			}
		}
		if (tmp != NULL) {
			AppstreamAppIdKind id_kind;
			id_kind = appstream_cache_app_id_kind_from_string (tmp);
			appstream_app_set_id_kind (helper->item_temp, id_kind);
		} else {
			g_warning ("no type in <id>, assuming 'desktop'");
			appstream_app_set_id_kind (helper->item_temp,
						   APPSTREAM_APP_ID_KIND_DESKTOP);
		}
		break;

	case APPSTREAM_TAG_SCREENSHOT:
		if (helper->item_temp == NULL ||
		    helper->tag != APPSTREAM_TAG_SCREENSHOTS) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get the screenshot kind */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				tmp = attribute_values[i];
				break;
			}
		}

		/* create screenshot */
		helper->screenshot = appstream_screenshot_new ();
		if (tmp != NULL) {
			AppstreamScreenshotKind kind;
			kind = appstream_screenshot_kind_from_string (tmp);
			appstream_screenshot_set_kind (helper->screenshot, kind);
		}
		break;

	case APPSTREAM_TAG_LANG:
		if (helper->tag != APPSTREAM_TAG_LANGUAGES) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get the lang percentage */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "percentage") == 0) {
				tmp = attribute_values[i];
				break;
			}
		}

		/* save language percentage */
		if (tmp == NULL)
			helper->priority = 100;
		else
			helper->priority = atoi (tmp);
		break;

	case APPSTREAM_TAG_CAPTION:
		if (helper->screenshot == NULL ||
		    helper->tag != APPSTREAM_TAG_SCREENSHOT) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get lang */
		if (!g_markup_collect_attributes (element_name,
						  attribute_names,
						  attribute_values,
						  error,
						  G_MARKUP_COLLECT_STRDUP | G_MARKUP_COLLECT_OPTIONAL,
						  "xml:lang", &helper->lang_temp,
						  G_MARKUP_COLLECT_INVALID))
			return;
		if (!helper->lang_temp)
			helper->lang_temp = g_strdup ("C");
		break;

	case APPSTREAM_TAG_VALUE:
		if (helper->tag != APPSTREAM_TAG_METADATA) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get lang */
		if (!g_markup_collect_attributes (element_name,
						  attribute_names,
						  attribute_values,
						  error,
						  G_MARKUP_COLLECT_STRDUP,
						  "key", &helper->lang_temp,
						  G_MARKUP_COLLECT_INVALID))
			return;
		break;

	case APPSTREAM_TAG_IMAGE:
		if (helper->item_temp == NULL ||
		    helper->tag != APPSTREAM_TAG_SCREENSHOT) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get the image attributes */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0)
				tmp = attribute_values[i];
			else if (g_strcmp0 (attribute_names[i], "width") == 0)
				width = atoi (attribute_values[i]);
			else if (g_strcmp0 (attribute_names[i], "height") == 0)
				height = atoi (attribute_values[i]);
		}

		/* create image */
		helper->image = appstream_image_new ();
		appstream_image_set_width (helper->image, width);
		appstream_image_set_height (helper->image, height);
		if (tmp != NULL) {
			AppstreamImageKind kind;
			kind = appstream_image_kind_from_string (tmp);
			appstream_image_set_kind (helper->image, kind);
		}
		break;
	case APPSTREAM_TAG_APPLICATION:
		if (helper->item_temp != NULL ||
		    helper->tag != APPSTREAM_TAG_APPLICATIONS) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}
		appstream_markup_reset (helper->markup);
		helper->item_temp = appstream_app_new ();
		appstream_app_set_priority (helper->item_temp, helper->priority);
		appstream_app_set_userdata (helper->item_temp,
					    (gpointer) helper->path_icons,
					    NULL);
		break;

	case APPSTREAM_TAG_ICON:
		/* get the icon kind */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				tmp = attribute_values[i];
				break;
			}
		}
		if (tmp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "icon type not set");
		} else {
			icon_kind = appstream_cache_icon_kind_from_string (tmp);
			if (icon_kind == APPSTREAM_APP_ICON_KIND_UNKNOWN) {
				g_set_error (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "icon type '%s' not supported", tmp);
			} else {
				appstream_app_set_icon_kind (helper->item_temp,
							     icon_kind);
			}
		}
		break;
	case APPSTREAM_TAG_URL:
		if (helper->item_temp == NULL ||
		    helper->tag != APPSTREAM_TAG_APPLICATION) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}

		/* get the url kind */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				helper->url_type_temp = g_strdup (attribute_values[i]);
				break;
			}
		}
		if (helper->url_type_temp == NULL)
			helper->url_type_temp = g_strdup ("homepage");
		break;
	case APPSTREAM_TAG_PKGNAME:
	case APPSTREAM_TAG_PROJECT_LICENSE:
	case APPSTREAM_TAG_PROJECT_GROUP:
		if (helper->item_temp == NULL ||
		    helper->tag != APPSTREAM_TAG_APPLICATION) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}
		break;

	case APPSTREAM_TAG_NAME:
	case APPSTREAM_TAG_SUMMARY:
		if (helper->item_temp == NULL ||
		    helper->tag != APPSTREAM_TAG_APPLICATION) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, tag %s",
				     element_name,
				     appstream_tag_to_string (helper->tag));
			return;
		}
		if (!g_markup_collect_attributes (element_name, attribute_names, attribute_values, error,
						  G_MARKUP_COLLECT_STRDUP | G_MARKUP_COLLECT_OPTIONAL,
						  "xml:lang", &helper->lang_temp,
						  G_MARKUP_COLLECT_INVALID))
			return;
		if (!helper->lang_temp)
			helper->lang_temp = g_strdup ("C");
		break;
	case APPSTREAM_TAG_DESCRIPTION:
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "xml:lang") == 0) {
				appstream_markup_set_lang (helper->markup,
							   attribute_values[i]);
				set_lang = TRUE;
				break;
			}
		}
		if (!set_lang)
			appstream_markup_set_lang (helper->markup, "C");
		appstream_markup_set_enabled (helper->markup, TRUE);
		appstream_markup_set_mode (helper->markup,
					   APPSTREAM_MARKUP_MODE_START);
		break;
	default:
		/* ignore unknown entries */
		if (helper->tag != APPSTREAM_TAG_UNKNOWN)
			helper->tag_last_known = helper->tag;
		break;
	}

	/* save */
	helper->tag = section_new;
}

/**
 * appstream_cache_add_item:
 */
static void
appstream_cache_add_item (AppstreamCacheHelper *helper)
{
	AppstreamApp *item;
	AppstreamAppIdKind id_kind;
	AppstreamCachePrivate *priv = helper->cache->priv;
	GPtrArray *pkgnames;
	const gchar *id;
	const gchar *pkgname;
	guint i;

	/* have we recorded this before? */
	id = appstream_app_get_id (helper->item_temp);
	item = g_hash_table_lookup (priv->hash_id, id);
	if (item != NULL) {

		/* the previously stored app is higher priority */
		if (appstream_app_get_priority (item) >
		    appstream_app_get_priority (helper->item_temp)) {
			g_debug ("ignoring duplicate AppStream entry: %s", id);
			appstream_app_free (helper->item_temp);
			return;
		}

		/* this new item has a higher priority than the one we've
		 * previously stored */
		g_debug ("replacing duplicate AppStream entry: %s", id);
		g_hash_table_remove (priv->hash_id, id);
		g_ptr_array_remove (priv->array, item);
	}

	/* this is a type we don't know how to handle */
	id_kind = appstream_app_get_id_kind (helper->item_temp);
	if (id_kind == APPSTREAM_APP_ID_KIND_UNKNOWN) {
		g_debug ("No idea how to handle AppStream entry: %s", id);
		appstream_app_free (helper->item_temp);
		return;
	}

	/* success, add to array */
	g_ptr_array_add (priv->array, helper->item_temp);
	g_hash_table_insert (priv->hash_id,
			     (gpointer) appstream_app_get_id (helper->item_temp),
			     helper->item_temp);
	pkgnames = appstream_app_get_pkgnames (helper->item_temp);
	for (i = 0; i < pkgnames->len; i++) {
		pkgname = g_ptr_array_index (pkgnames, i);
		g_hash_table_insert (priv->hash_pkgname,
				     g_strdup (pkgname),
				     helper->item_temp);
	}
}

/**
 * appstream_cache_end_element_cb:
 */
static void
appstream_cache_end_element_cb (GMarkupParseContext *context,
				const gchar *element_name,
				gpointer user_data,
				GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	AppstreamTag section_new;
	const gchar *lang;
	const gchar *tmp;

	if (helper->tag == APPSTREAM_TAG_DESCRIPTION) {
		if (g_strcmp0 (element_name, "p") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_P_END);
		} else if (g_strcmp0 (element_name, "ul") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_UL_END);
		} else if (g_strcmp0 (element_name, "li") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_LI_END);
		} else if (g_strcmp0 (element_name, "description") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_END);
			tmp = appstream_markup_get_text (helper->markup);
			if (tmp != NULL) {
				lang = appstream_markup_get_lang (helper->markup);
				appstream_app_set_description (helper->item_temp,
							       lang, tmp, -1);
			}
			helper->tag = APPSTREAM_TAG_APPLICATION;
		}
		return;
	}

	section_new = appstream_tag_from_string (element_name);
	switch (section_new) {
	case APPSTREAM_TAG_APPLICATIONS:
	case APPSTREAM_TAG_APPCATEGORY:
	case APPSTREAM_TAG_KEYWORD:
	case APPSTREAM_TAG_MIMETYPE:
		/* ignore */
		break;
	case APPSTREAM_TAG_APPLICATION:
		/* perhaps add application */
		appstream_cache_add_item (helper);
		helper->item_temp = NULL;
		helper->tag = APPSTREAM_TAG_APPLICATIONS;
		break;
	case APPSTREAM_TAG_IMAGE:
		appstream_screenshot_add_image (helper->screenshot,
						helper->image);
		helper->image = NULL;
		helper->tag = APPSTREAM_TAG_SCREENSHOT;
		break;
	case APPSTREAM_TAG_SCREENSHOT:
		appstream_app_add_screenshot (helper->item_temp,
					      helper->screenshot);
		helper->screenshot = NULL;
		helper->tag = APPSTREAM_TAG_SCREENSHOTS;
		break;
	case APPSTREAM_TAG_LANG:
		helper->tag = APPSTREAM_TAG_LANGUAGES;
		break;
	case APPSTREAM_TAG_LANGUAGES:
		helper->tag = APPSTREAM_TAG_APPLICATION;
		break;
	case APPSTREAM_TAG_ID:
	case APPSTREAM_TAG_PKGNAME:
	case APPSTREAM_TAG_APPCATEGORIES:
	case APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP:
	case APPSTREAM_TAG_KEYWORDS:
	case APPSTREAM_TAG_MIMETYPES:
	case APPSTREAM_TAG_METADATA:
	case APPSTREAM_TAG_PROJECT_LICENSE:
	case APPSTREAM_TAG_ICON:
		helper->tag = APPSTREAM_TAG_APPLICATION;
		break;
	case APPSTREAM_TAG_CAPTION:
		helper->tag = APPSTREAM_TAG_SCREENSHOT;
		break;
	case APPSTREAM_TAG_URL:
		g_free (helper->url_type_temp);
		helper->tag = APPSTREAM_TAG_APPLICATION;
		break;
	case APPSTREAM_TAG_NAME:
	case APPSTREAM_TAG_SUMMARY:
	case APPSTREAM_TAG_PROJECT_GROUP:
		helper->tag = APPSTREAM_TAG_APPLICATION;
		g_free (helper->lang_temp);
		helper->lang_temp = NULL;
		break;
	case APPSTREAM_TAG_PRIORITY:
		helper->tag = APPSTREAM_TAG_APPLICATIONS;
		break;
	case APPSTREAM_TAG_VALUE:
		g_free (helper->lang_temp);
		helper->lang_temp = NULL;
		helper->tag = APPSTREAM_TAG_METADATA;
		break;
	default:
		/* ignore unknown entries */
		helper->tag = helper->tag_last_known;
		break;
	}
}

/**
 * appstream_cache_text_cb:
 */
static void
appstream_cache_text_cb (GMarkupParseContext *context,
			 const gchar *text,
			 gsize text_len,
			 gpointer user_data,
			 GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;

	switch (helper->tag) {
	case APPSTREAM_TAG_UNKNOWN:
	case APPSTREAM_TAG_APPLICATIONS:
	case APPSTREAM_TAG_APPLICATION:
	case APPSTREAM_TAG_APPCATEGORIES:
	case APPSTREAM_TAG_KEYWORDS:
	case APPSTREAM_TAG_MIMETYPES:
	case APPSTREAM_TAG_METADATA:
		/* ignore */
		break;
	case APPSTREAM_TAG_PRIORITY:
		if (helper->item_temp != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp priority invalid");
			return;
		}
		helper->priority = atoi (text);
		break;
	case APPSTREAM_TAG_APPCATEGORY:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp category invalid");
			return;
		}
		appstream_app_add_category (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_KEYWORD:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp category invalid");
			return;
		}
		appstream_app_add_keyword (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_MIMETYPE:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp mimetype invalid");
			return;
		}
		appstream_app_add_mimetype (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_VALUE:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp mimetype invalid");
			return;
		}
		appstream_app_add_metadata (helper->item_temp,
					    helper->lang_temp,
					    text,
					    text_len);
		break;
	case APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp category invalid");
			return;
		}
		appstream_app_add_desktop_core (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_ID:
		if (helper->item_temp == NULL ||
		    appstream_app_get_id (helper->item_temp) != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp id invalid");
			return;
		}
		appstream_app_set_id (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_PKGNAME:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp pkgname invalid");
			return;
		}
		appstream_app_add_pkgname (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_NAME:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp name invalid");
			return;
		}
		appstream_app_set_name (helper->item_temp, helper->lang_temp, text, text_len);
		break;
	case APPSTREAM_TAG_SUMMARY:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp summary invalid");
			return;
		}
		appstream_app_set_summary (helper->item_temp, helper->lang_temp, text, text_len);
		break;
	case APPSTREAM_TAG_PROJECT_GROUP:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp project_group invalid");
			return;
		}
		appstream_app_set_project_group (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_URL:
		if (helper->item_temp == NULL ||
		    helper->url_type_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp url invalid");
			return;
		}
		appstream_app_add_url (helper->item_temp,
				       helper->url_type_temp,
				       text, text_len);
		break;
	case APPSTREAM_TAG_PROJECT_LICENSE:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp license invalid");
			return;
		}
		if (appstream_app_get_project_license (helper->item_temp) == NULL) {
			appstream_app_set_project_license (helper->item_temp,
							   text,
							   text_len);
		}
		break;
	case APPSTREAM_TAG_DESCRIPTION:
		appstream_markup_add_content (helper->markup, text, text_len);
		break;
	case APPSTREAM_TAG_ICON:
		if (helper->item_temp == NULL ||
		    appstream_app_get_icon (helper->item_temp) != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp icon invalid");
			return;
		}
		appstream_app_set_icon (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_TAG_IMAGE:
		if (helper->image == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "image not started");
			return;
		}
		appstream_image_set_url (helper->image, text, text_len);
		break;
	case APPSTREAM_TAG_CAPTION:
		if (helper->screenshot == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "screesnhot not started");
			return;
		}
		appstream_screenshot_set_caption (helper->screenshot,
						  helper->lang_temp,
						  text,
						  text_len);
		break;
	case APPSTREAM_TAG_LANG:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "lang not started");
			return;
		}
		appstream_app_add_locale (helper->item_temp,
					  text,
					  text_len,
					  helper->priority);
		break;
	default:
		/* ignore unknown entries */
		break;
	}
}

/**
 * appstream_cache_parse_file:
 */
gboolean
appstream_cache_parse_file (AppstreamCache *cache,
			    GFile *file,
			    const gchar *path_icons,
			    GCancellable *cancellable,
			    GError **error)
{
	const gchar *content_type = NULL;
	gboolean ret = TRUE;
	gchar *data = NULL;
	gchar *icon_path_tmp = NULL;
	GConverter *converter = NULL;
	GFileInfo *info = NULL;
	GInputStream *file_stream;
	GInputStream *stream_data = NULL;
	GMarkupParseContext *ctx = NULL;
	AppstreamCacheHelper *helper = NULL;
	gssize len;
	const GMarkupParser parser = {
		appstream_cache_start_element_cb,
		appstream_cache_end_element_cb,
		appstream_cache_text_cb,
		NULL /* passthrough */,
		NULL /* error */ };

	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), FALSE);

	file_stream = G_INPUT_STREAM (g_file_read (file, cancellable, error));
	if (file_stream == NULL) {
		ret = FALSE;
		goto out;
	}

	/* what kind of file is this */
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  error);
	if (info == NULL) {
		ret = FALSE;
		goto out;
	}

	/* decompress if required */
	content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	if (g_strcmp0 (content_type, "application/gzip") == 0 ||
	    g_strcmp0 (content_type, "application/x-gzip") == 0) {
		converter = G_CONVERTER (g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP));
		stream_data = g_converter_input_stream_new (file_stream, converter);
	} else if (g_strcmp0 (content_type, "application/xml") == 0) {
		stream_data = g_object_ref (file_stream);
	} else {
		ret = FALSE;
		g_set_error (error,
			     APPSTREAM_CACHE_ERROR,
			     APPSTREAM_CACHE_ERROR_FAILED,
			     "cannot process file of type %s",
			     content_type);
		goto out;
	}

	/* add to array to maintain a ref for the lifetime of the AppstreamApp */
	icon_path_tmp = g_strdup (path_icons);
	g_ptr_array_add (cache->priv->icon_path_array, icon_path_tmp);

	helper = g_new0 (AppstreamCacheHelper, 1);
	helper->cache = cache;
	helper->markup = appstream_markup_new ();
	helper->path_icons = icon_path_tmp;
	ctx = g_markup_parse_context_new (&parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  helper,
					  NULL);
	data = g_malloc (32 * 1024);
	while ((len = g_input_stream_read (stream_data, data, 32 * 1024, NULL, error)) > 0) {
		ret = g_markup_parse_context_parse (ctx, data, len, error);
		if (!ret)
			goto out;
	}
	if (len < 0)
		ret = FALSE;
out:
	if (helper != NULL && helper->item_temp != NULL) {
		appstream_markup_free (helper->markup);
		appstream_app_free (helper->item_temp);
	}
	if (info != NULL)
		g_object_unref (info);
	g_free (helper);
	g_free (data);
	if (ctx != NULL)
		g_markup_parse_context_free (ctx);
	if (stream_data != NULL)
		g_object_unref (stream_data);
	if (converter != NULL)
		g_object_unref (converter);
	g_object_unref (file_stream);
	return ret;
}

/**
 * appstream_cache_class_init:
 **/
static void
appstream_cache_class_init (AppstreamCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = appstream_cache_finalize;
	g_type_class_add_private (klass, sizeof (AppstreamCachePrivate));
}

/**
 * appstream_cache_init:
 **/
static void
appstream_cache_init (AppstreamCache *cache)
{
	AppstreamCachePrivate *priv;

	priv = cache->priv = APPSTREAM_CACHE_GET_PRIVATE (cache);
	priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) appstream_app_free);
	priv->icon_path_array = g_ptr_array_new_with_free_func (g_free);
	priv->hash_id = g_hash_table_new_full (g_str_hash,
					       g_str_equal,
					       NULL,
					       NULL);
	priv->hash_pkgname = g_hash_table_new_full (g_str_hash,
						    g_str_equal,
						    g_free,
						    NULL);
}

/**
 * appstream_cache_finalize:
 **/
static void
appstream_cache_finalize (GObject *object)
{
	AppstreamCache *cache = APPSTREAM_CACHE (object);
	AppstreamCachePrivate *priv = cache->priv;

	g_ptr_array_unref (priv->array);
	g_ptr_array_unref (priv->icon_path_array);
	g_hash_table_unref (priv->hash_id);
	g_hash_table_unref (priv->hash_pkgname);

	G_OBJECT_CLASS (appstream_cache_parent_class)->finalize (object);
}

/**
 * appstream_cache_new:
 **/
AppstreamCache *
appstream_cache_new (void)
{
	AppstreamCache *cache;
	cache = g_object_new (APPSTREAM_TYPE_CACHE, NULL);
	return APPSTREAM_CACHE (cache);
}
