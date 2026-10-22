/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <thunar/thunar-recent.h>

typedef struct
{
  GFileEnumerator parent;
  GPtrArray      *infos;
  guint           position;
  gint            fail_at;
} TestEnumerator;
typedef GFileEnumeratorClass TestEnumeratorClass;
static GType test_enumerator_get_type (void);
G_DEFINE_TYPE (TestEnumerator, test_enumerator, G_TYPE_FILE_ENUMERATOR)

static GFileInfo *
next_file (GFileEnumerator *enumerator, GCancellable *cancellable, GError **error)
{
  TestEnumerator *self = (TestEnumerator *) enumerator;
  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return NULL;
  if ((gint) self->position == self->fail_at)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Test enumeration error");
      return NULL;
    }
  return self->position < self->infos->len ? g_object_ref (g_ptr_array_index (self->infos, self->position++)) : NULL;
}

static void
finalize (GObject *object)
{
  g_ptr_array_unref (((TestEnumerator *) object)->infos);
  G_OBJECT_CLASS (test_enumerator_parent_class)->finalize (object);
}

static gboolean
close_enumerator (GFileEnumerator *enumerator, GCancellable *cancellable, GError **error)
{
  return TRUE;
}

static void
test_enumerator_class_init (TestEnumeratorClass *klass)
{
  klass->next_file = next_file;
  klass->close_fn = close_enumerator;
  G_OBJECT_CLASS (klass)->finalize = finalize;
}

static void
test_enumerator_init (TestEnumerator *self)
{
  self->infos = g_ptr_array_new_with_free_func (g_object_unref);
  self->fail_at = -1;
}

static TestEnumerator *
make_enumerator (guint count)
{
  TestEnumerator *self = g_object_new (test_enumerator_get_type (), NULL);
  for (guint i = 0; i < count; ++i)
    {
      GFileInfo *info = g_file_info_new ();
      gchar *uri = g_strdup_printf ("file:///test/%u", i);
      /* Deliberately not chronological or reverse chronological. */
      g_file_info_set_attribute_int64 (info, G_FILE_ATTRIBUTE_RECENT_MODIFIED, (i * 37) % 503);
      g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI, uri);
      g_ptr_array_add (self->infos, info);
      g_free (uri);
    }
  return self;
}

static void
test_newest_200 (void)
{
  TestEnumerator *enumerator = make_enumerator (503);
  GError *error = NULL;
  GList *infos = thunar_recent_collect_infos (G_FILE_ENUMERATOR (enumerator), THUNAR_RECENT_FILES_LIMIT, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (g_list_length (infos), ==, 200);
  gint64 expected = 502;
  for (GList *lp = infos; lp != NULL; lp = lp->next)
    g_assert_cmpint (g_file_info_get_attribute_int64 (lp->data, G_FILE_ATTRIBUTE_RECENT_MODIFIED), ==, expected--);
  g_assert_cmpuint (enumerator->position, ==, 503);
  g_list_free_full (infos, g_object_unref);
  g_object_unref (enumerator);
}

static void
test_errors_and_small_lists (void)
{
  TestEnumerator *enumerator = make_enumerator (3);
  GError *error = NULL;
  GList *infos = thunar_recent_collect_infos (G_FILE_ENUMERATOR (enumerator), 200, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (g_list_length (infos), ==, 3);
  g_list_free_full (infos, g_object_unref);
  g_object_unref (enumerator);

  enumerator = make_enumerator (40);
  enumerator->fail_at = 20;
  infos = thunar_recent_collect_infos (G_FILE_ENUMERATOR (enumerator), 200, NULL, &error);
  g_assert_null (infos);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);
  g_object_unref (enumerator);

  enumerator = make_enumerator (40);
  GCancellable *cancellable = g_cancellable_new ();
  g_cancellable_cancel (cancellable);
  infos = thunar_recent_collect_infos (G_FILE_ENUMERATOR (enumerator), 200, cancellable, &error);
  g_assert_null (infos);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);
  g_object_unref (cancellable);
  g_object_unref (enumerator);
}

int main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/recent/newest-200", test_newest_200);
  g_test_add_func ("/recent/errors-and-small-lists", test_errors_and_small_lists);
  return g_test_run ();
}
