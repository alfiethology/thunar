/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __THUNAR_RG_SEARCH_H__
#define __THUNAR_RG_SEARCH_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/* Paths are relative to the search directory, in filename encoding. */
typedef void (*ThunarRgSearchFunc) (const gchar *path,
                                   gpointer     user_data);

gboolean thunar_rg_search (GFile              *directory,
                           gboolean            show_hidden,
                           GCancellable       *cancellable,
                           ThunarRgSearchFunc  callback,
                           gpointer            user_data,
                           GError            **error);

G_END_DECLS

#endif /* !__THUNAR_RG_SEARCH_H__ */
