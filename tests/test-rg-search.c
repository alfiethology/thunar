/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <glib/gstdio.h>
#include <thunar/thunar-rg-search.h>

typedef struct
{
  gchar        *path;
  GFile        *directory;
  GCancellable *cancellable;
  GHashTable   *paths;
} Fixture;

static void
remove_tree (GFile *file)
{
  GFileEnumerator *enumerator;
  GFileInfo       *info;

  enumerator = g_file_enumerate_children (file, "standard::name",
                                         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
  if (enumerator != NULL)
    {
      while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
        {
          GFile *child = g_file_get_child (file, g_file_info_get_name (info));
          if (g_file_query_file_type (child, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) == G_FILE_TYPE_DIRECTORY)
            remove_tree (child);
          else
            g_assert_true (g_file_delete (child, NULL, NULL));
          g_object_unref (child);
          g_object_unref (info);
        }
      g_object_unref (enumerator);
    }
  g_assert_true (g_file_delete (file, NULL, NULL));
}

static void
setup (Fixture *fixture,
       gconstpointer unused)
{
  fixture->path = g_dir_make_tmp ("thunar-rg-test-XXXXXX", NULL);
  g_assert_nonnull (fixture->path);
  fixture->directory = g_file_new_for_path (fixture->path);
  fixture->cancellable = g_cancellable_new ();
  fixture->paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
teardown (Fixture *fixture,
          gconstpointer unused)
{
  remove_tree (fixture->directory);
  g_object_unref (fixture->directory);
  g_object_unref (fixture->cancellable);
  g_hash_table_unref (fixture->paths);
  g_free (fixture->path);
}

static void
write_file (Fixture    *fixture,
            const gchar *name,
            const gchar *contents)
{
  gchar *path = g_build_filename (fixture->path, name, NULL);
  gchar *parent = g_path_get_dirname (path);
  g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
  g_assert_true (g_file_set_contents (path, contents, -1, NULL));
  g_free (parent);
  g_free (path);
}

static void
collect (const gchar *path,
         gpointer     user_data)
{
  Fixture *fixture = user_data;
  g_assert_true (g_str_has_prefix (path, "./"));
  g_hash_table_add (fixture->paths, g_strdup (path + 2));
}

static void
test_files (Fixture *fixture,
            gconstpointer unused)
{
  GError *error = NULL;
  GFile  *link;

  write_file (fixture, "top.txt", "contents are irrelevant");
  write_file (fixture, "sub/deeper/file.txt", "");
  write_file (fixture, "sub/space newline\n--[x]$().txt", "");
  write_file (fixture, "sub/non-utf8-\xff", "");
  write_file (fixture, ".ignore", "*.txt\nsub/\n");
  write_file (fixture, ".gitignore", "*.txt\n");
  write_file (fixture, ".hidden/inside.txt", "");
  write_file (fixture, ".hidden-file", "");
  write_file (fixture, "backup~", "");
  write_file (fixture, "backup-dir~/inside.txt", "");

  /* A symlink loop must never be traversed. */
  link = g_file_get_child (fixture->directory, "sub/loop");
  g_assert_true (g_file_make_symbolic_link (link, "..", NULL, &error));
  g_assert_no_error (error);
  g_object_unref (link);

  g_assert_true (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                  collect, fixture, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (g_hash_table_size (fixture->paths), ==, 4);
  g_assert_true (g_hash_table_contains (fixture->paths, "top.txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/deeper/file.txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/space newline\n--[x]$().txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/non-utf8-\xff"));

  g_hash_table_remove_all (fixture->paths);
  g_assert_true (thunar_rg_search (fixture->directory, TRUE, fixture->cancellable,
                                  collect, fixture, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (g_hash_table_size (fixture->paths), ==, 10);
  g_assert_true (g_hash_table_contains (fixture->paths, ".hidden/inside.txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "backup-dir~/inside.txt"));
}

static void
test_empty (Fixture *fixture,
            gconstpointer unused)
{
  GError *error = NULL;
  g_assert_true (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                  collect, fixture, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (g_hash_table_size (fixture->paths), ==, 0);
}

static void
test_pre_cancelled (Fixture *fixture,
                    gconstpointer unused)
{
  GError *error = NULL;
  g_cancellable_cancel (fixture->cancellable);
  g_assert_false (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                   collect, fixture, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);
}

static gpointer
cancel_later (gpointer user_data)
{
  g_usleep (100 * 1000);
  g_cancellable_cancel (user_data);
  return NULL;
}

static void
test_process_errors (Fixture *fixture,
                     gconstpointer unused)
{
  gchar   *executable;
  GError  *error = NULL;
  GThread *thread;
  gint64   started;

  /* Change PATH only in a fresh process, before GSubprocess creates threads. */
  if (!g_test_subprocess ())
    {
      g_test_trap_subprocess (NULL, 5 * G_USEC_PER_SEC, 0);
      g_test_trap_assert_passed ();
      return;
    }

  executable = g_build_filename (fixture->path, "rg", NULL);
  g_setenv ("PATH", fixture->path, TRUE);
  g_assert_false (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                   collect, fixture, &error));
  g_assert_nonnull (error);
  g_clear_error (&error);

  write_file (fixture, "rg", "#!/bin/sh\nexit 2\n");
  g_assert_cmpint (g_chmod (executable, 0700), ==, 0);
  g_assert_false (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                   collect, fixture, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);

  /* Verify cancellation kills a child blocked without producing output. */
  write_file (fixture, "rg", "#!/bin/sh\nexec /bin/sleep 30\n");
  g_assert_cmpint (g_chmod (executable, 0700), ==, 0);
  started = g_get_monotonic_time ();
  thread = g_thread_new ("cancel-search", cancel_later, fixture->cancellable);
  g_assert_false (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                   collect, fixture, &error));
  g_thread_join (thread);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpint (g_get_monotonic_time () - started, <, 2 * G_USEC_PER_SEC);
  g_clear_error (&error);

  g_free (executable);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/rg/files", Fixture, NULL, setup, test_files, teardown);
  g_test_add ("/rg/empty", Fixture, NULL, setup, test_empty, teardown);
  g_test_add ("/rg/pre-cancelled", Fixture, NULL, setup, test_pre_cancelled, teardown);
  g_test_add ("/rg/process-errors", Fixture, NULL, setup, test_process_errors, teardown);
  return g_test_run ();
}
