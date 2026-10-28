/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __THUNAR_RG_SEARCH_H__
#define __THUNAR_RG_SEARCH_H__

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
  THUNAR_SEARCH_NAME_CONTAINS,
  THUNAR_SEARCH_NAME_WILDCARD,
  THUNAR_SEARCH_NAME_REGEX
} ThunarSearchNameMode;

typedef struct
{
  gchar    *text;
  gboolean  exclude;
  gboolean  in_contents; /* FALSE searches the basename, never its parent path. */
  gboolean  is_directory; /* text is an absolute search-root/excluded-subtree path */
} ThunarSearchRule;

typedef struct
{
  gchar                *name;
  gchar                *content;
  GPtrArray            *rules; /* owned ThunarSearchRule entries; all are combined with AND */
  ThunarSearchNameMode   name_mode;
  gboolean              case_sensitive;
  gboolean              recursive;
  gboolean              show_hidden;
  gboolean              exclude_directories;
  guint64               min_size;
  guint64               max_size;
  gint64                modified_after;
  gint64                modified_before; /* exclusive */
} ThunarSearchOptions;

#define THUNAR_TYPE_SEARCH_OPTIONS (thunar_search_options_get_type ())
GType                thunar_search_options_get_type  (void) G_GNUC_CONST;
ThunarSearchOptions *thunar_search_options_new       (void);
void                 thunar_search_options_add_rule (ThunarSearchOptions *options,
                                                     const gchar         *text,
                                                     gboolean             exclude,
                                                     gboolean             in_contents);
void                 thunar_search_options_add_directory (ThunarSearchOptions *options,
                                                          const gchar         *path,
                                                          gboolean             exclude);
ThunarSearchOptions *thunar_search_options_copy      (const ThunarSearchOptions *options);
void                 thunar_search_options_free      (ThunarSearchOptions       *options);
gboolean             thunar_search_options_validate  (const ThunarSearchOptions *options,
                                                      GError                   **error);
gboolean             thunar_search_options_set_dates (ThunarSearchOptions       *options,
                                                      const gchar               *after,
                                                      const gchar               *before,
                                                      GError                   **error);

/* Paths are relative to the main search directory, or absolute for added roots,
 * in filename encoding. */
typedef void (*ThunarRgSearchFunc) (const gchar *path,
                                   gpointer     user_data);

gboolean thunar_rg_search (GFile              *directory,
                           gboolean            show_hidden,
                           GCancellable       *cancellable,
                           ThunarRgSearchFunc  callback,
                           gpointer            user_data,
                           GError            **error);

gboolean thunar_rg_search_full (GFile                     *directory,
                                const ThunarSearchOptions *options,
                                GCancellable              *cancellable,
                                ThunarRgSearchFunc         callback,
                                gpointer                   user_data,
                                GError                   **error);

G_END_DECLS

#endif /* !__THUNAR_RG_SEARCH_H__ */
