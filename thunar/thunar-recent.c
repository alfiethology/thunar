/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "thunar-recent.h"

static gint
compare_recent (gconstpointer a,
                gconstpointer b,
                gpointer      unused)
{
  gint64 first = g_file_info_get_attribute_int64 (G_FILE_INFO (a), G_FILE_ATTRIBUTE_RECENT_MODIFIED);
  gint64 second = g_file_info_get_attribute_int64 (G_FILE_INFO (b), G_FILE_ATTRIBUTE_RECENT_MODIFIED);
  if (first != second)
    return first > second ? -1 : 1;
  return g_strcmp0 (g_file_info_get_attribute_string (G_FILE_INFO (a), G_FILE_ATTRIBUTE_STANDARD_TARGET_URI),
                    g_file_info_get_attribute_string (G_FILE_INFO (b), G_FILE_ATTRIBUTE_STANDARD_TARGET_URI));
}

GList *
thunar_recent_collect_infos (GFileEnumerator *enumerator,
                             guint            limit,
                             GCancellable    *cancellable,
                             GError         **error)
{
  GSequence *newest;
  GFileInfo *info;
  GList     *infos = NULL;
  GError    *err = NULL;

  if (g_cancellable_set_error_if_cancelled (cancellable, error) || limit == 0)
    return NULL;

  /* GVfs does not guarantee enumeration order. Retain only the newest entries
   * before querying their target files, keeping memory and metadata work bounded. */
  newest = g_sequence_new (g_object_unref);
  while ((info = g_file_enumerator_next_file (enumerator, cancellable, &err)) != NULL)
    {
      g_sequence_insert_sorted (newest, info, compare_recent, NULL);
      if ((guint) g_sequence_get_length (newest) > limit)
        g_sequence_remove (g_sequence_iter_prev (g_sequence_get_end_iter (newest)));
    }
  if (err != NULL)
    {
      g_sequence_free (newest);
      g_propagate_error (error, err);
      return NULL;
    }
  for (GSequenceIter *iter = g_sequence_get_begin_iter (newest);
       !g_sequence_iter_is_end (iter); iter = g_sequence_iter_next (iter))
    infos = g_list_prepend (infos, g_object_ref (g_sequence_get (iter)));
  g_sequence_free (newest);
  return g_list_reverse (infos);
}
