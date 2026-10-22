/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __THUNAR_ADVANCED_SEARCH_DIALOG_H__
#define __THUNAR_ADVANCED_SEARCH_DIALOG_H__

#include <gtk/gtk.h>
#include <thunar/thunar-rg-search.h>

GtkWidget           *thunar_advanced_search_dialog_new           (GtkWindow                 *parent,
                                                                 GFile                     *directory,
                                                                 const ThunarSearchOptions *options);
ThunarSearchOptions *thunar_advanced_search_dialog_get_options   (GtkWidget                 *dialog,
                                                                 GError                   **error);
GFile               *thunar_advanced_search_dialog_get_directory (GtkWidget                 *dialog);

#endif
