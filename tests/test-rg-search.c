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
  g_assert_cmpuint (g_hash_table_size (fixture->paths), ==, 6);
  g_assert_true (g_hash_table_contains (fixture->paths, "sub"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/deeper"));
  g_assert_true (g_hash_table_contains (fixture->paths, "top.txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/deeper/file.txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/space newline\n--[x]$().txt"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/non-utf8-\xff"));

  g_hash_table_remove_all (fixture->paths);
  g_assert_true (thunar_rg_search (fixture->directory, TRUE, fixture->cancellable,
                                  collect, fixture, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (g_hash_table_size (fixture->paths), ==, 14);
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
run_filtered (Fixture                   *fixture,
              const ThunarSearchOptions *options,
              guint                      expected_count)
{
  GError *error = NULL;
  g_hash_table_remove_all (fixture->paths);
  g_assert_true (thunar_rg_search_full (fixture->directory, options, fixture->cancellable,
                                       collect, fixture, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (g_hash_table_size (fixture->paths), ==, expected_count);
}

static void
test_inaccessible_paths (Fixture *fixture,
                         gconstpointer unused)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  gchar *locked = g_build_filename (fixture->path, "locked", NULL);

  write_file (fixture, "readable.txt", "needle");
  write_file (fixture, "locked/unreadable.txt", "needle");
  g_assert_cmpint (g_chmod (locked, 0000), ==, 0);
  if (g_access (locked, R_OK | X_OK) == 0)
    g_test_skip ("This user can read directories even without permission bits");
  else
    {
      run_filtered (fixture, options, 2); /* The unreadable directory itself is a result. */
      g_assert_true (g_hash_table_contains (fixture->paths, "readable.txt"));
      g_free (options->content);
      options->content = g_strdup ("");
      thunar_search_options_add_rule (options, "forbidden", TRUE, TRUE);
      run_filtered (fixture, options, 1);
      g_assert_true (g_hash_table_contains (fixture->paths, "readable.txt"));
      g_free (options->content);
      options->content = g_strdup ("needle");
      run_filtered (fixture, options, 1);
      g_assert_true (g_hash_table_contains (fixture->paths, "readable.txt"));
    }
  g_assert_cmpint (g_chmod (locked, 0700), ==, 0);
  g_free (locked);
  thunar_search_options_free (options);
}

static void
test_name_and_content (Fixture *fixture,
                       gconstpointer unused)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  write_file (fixture, "Report.TXT", "--Needle [a].");
  write_file (fixture, "sub/report.txt", "--needle [a].");
  write_file (fixture, "report.md", "--needle aX");
  write_file (fixture, "report/not-a-match.txt", "--needle [a].");
  write_file (fixture, ".hidden/report.txt", "--needle [a].");
  write_file (fixture, "Straße.TXT", "");

  g_free (options->name);
  options->name = g_strdup ("report");
  run_filtered (fixture, options, 4); /* Match the folder itself, not its nonmatching children. */
  options->recursive = FALSE;
  run_filtered (fixture, options, 3);
  options->recursive = TRUE;
  options->show_hidden = TRUE;
  run_filtered (fixture, options, 5);
  options->show_hidden = FALSE;
  options->case_sensitive = TRUE;
  run_filtered (fixture, options, 3);
  options->case_sensitive = FALSE;

  g_free (options->name);
  options->name = g_strdup ("report*.txt");
  options->name_mode = THUNAR_SEARCH_NAME_WILDCARD;
  run_filtered (fixture, options, 2);
  g_free (options->name);
  options->name = g_strdup ("^report\\.(txt|md)$");
  options->name_mode = THUNAR_SEARCH_NAME_REGEX;
  run_filtered (fixture, options, 3);
  g_free (options->content);
  options->content = g_strdup ("--needle [a].");
  run_filtered (fixture, options, 2); /* Literal content, safely passed as an argument. */
  options->case_sensitive = TRUE;
  run_filtered (fixture, options, 1);
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/report.txt"));

  options->case_sensitive = FALSE;
  g_free (options->name);
  options->name = g_strdup ("");
  run_filtered (fixture, options, 3); /* Empty regex means any filename. */
  g_free (options->content);
  options->content = g_strdup ("");
  g_free (options->name);
  options->name = g_strdup ("STRASSE txt");
  options->name_mode = THUNAR_SEARCH_NAME_CONTAINS;
  run_filtered (fixture, options, 1);
  g_assert_true (g_hash_table_contains (fixture->paths, "Straße.TXT"));
  thunar_search_options_free (options);
}

static void
test_directories (Fixture *fixture, gconstpointer unused)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  const gchar *directories[] = { "report empty", "sub/report folder", ".hidden/report", "backup~/report" };
  for (guint i = 0; i < G_N_ELEMENTS (directories); ++i)
    {
      gchar *path = g_build_filename (fixture->path, directories[i], NULL);
      g_assert_cmpint (g_mkdir_with_parents (path, 0700), ==, 0);
      g_free (path);
    }
  write_file (fixture, "report.txt", "needle");
  write_file (fixture, "sub/report.txt", "needle");
  write_file (fixture, "report empty/not-a-match.txt", "needle");
  g_free (options->name);
  options->name = g_strdup ("report");
  run_filtered (fixture, options, 4);
  g_assert_true (g_hash_table_contains (fixture->paths, "report empty"));
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/report folder"));
  g_assert_false (g_hash_table_contains (fixture->paths, "report empty/not-a-match.txt"));
  options->exclude_directories = TRUE;
  run_filtered (fixture, options, 2);
  g_assert_true (g_hash_table_contains (fixture->paths, "sub/report.txt"));
  options->exclude_directories = FALSE;
  options->recursive = FALSE;
  run_filtered (fixture, options, 2);
  options->recursive = TRUE;
  options->show_hidden = TRUE;
  run_filtered (fixture, options, 6);
  thunar_search_options_add_rule (options, "empty", TRUE, FALSE);
  run_filtered (fixture, options, 5);
  thunar_search_options_add_rule (options, "needle", FALSE, TRUE);
  run_filtered (fixture, options, 2); /* Content rules cannot match directories. */
  thunar_search_options_free (options);
}

static void
cancel_directory_result (const gchar *path, gpointer user_data)
{
  Fixture *fixture = user_data;
  collect (path, fixture);
  g_cancellable_cancel (fixture->cancellable);
}

static void
test_directory_cancellation (Fixture *fixture, gconstpointer unused)
{
  gchar *path = g_build_filename (fixture->path, "empty", NULL);
  GError *error = NULL;
  g_assert_cmpint (g_mkdir (path, 0700), ==, 0);
  g_assert_false (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                   cancel_directory_result, fixture, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_true (g_hash_table_contains (fixture->paths, "empty"));
  g_clear_error (&error);
  g_free (path);
}

static void
test_rules (Fixture *fixture, gconstpointer unused)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  write_file (fixture, "Report-final.txt", "ALPHA\nbeta\n");
  write_file (fixture, "Report-draft.txt", "alpha beta");
  write_file (fixture, "Report-missing.txt", "alpha");
  write_file (fixture, "Report-bad.txt", "alpha beta forbidden");
  write_file (fixture, "other-final.txt", "alpha beta");
  thunar_search_options_add_rule (options, "REPORT", FALSE, FALSE);
  thunar_search_options_add_rule (options, "draft", TRUE, FALSE);
  run_filtered (fixture, options, 3);
  thunar_search_options_add_rule (options, "alpha", FALSE, TRUE);
  thunar_search_options_add_rule (options, "beta", FALSE, TRUE);
  run_filtered (fixture, options, 2); /* Includes may occur on different lines. */
  thunar_search_options_add_rule (options, "forbidden", TRUE, TRUE);
  run_filtered (fixture, options, 1);
  g_assert_true (g_hash_table_contains (fixture->paths, "Report-final.txt"));
  options->max_size = 10;
  run_filtered (fixture, options, 0); /* Metadata filters also combine with rules. */
  options->max_size = G_MAXUINT64;
  options->case_sensitive = TRUE;
  run_filtered (fixture, options, 0);
  options->case_sensitive = FALSE;
  thunar_search_options_add_rule (options, "", TRUE, TRUE);
  run_filtered (fixture, options, 1); /* Blank exclusions never exclude everything. */

  ThunarSearchOptions *copy = thunar_search_options_copy (options);
  ThunarSearchRule *rule = g_ptr_array_index (options->rules, 0);
  g_free (rule->text);
  rule->text = g_strdup ("changed");
  run_filtered (fixture, copy, 1); /* Jobs and tabs own independent rule lists. */
  thunar_search_options_free (copy);
  g_ptr_array_set_size (options->rules, 0);
  thunar_search_options_add_rule (options, "forbidden", TRUE, TRUE);
  run_filtered (fixture, options, 4); /* Exclusion-only searches work. */
  g_ptr_array_set_size (options->rules, 0);
  thunar_search_options_add_rule (options, "report", TRUE, FALSE);
  run_filtered (fixture, options, 1);
  g_assert_true (g_hash_table_contains (fixture->paths, "other-final.txt"));
  write_file (fixture, "literal[1]--.txt", "[not a regex] --flag");
  g_ptr_array_set_size (options->rules, 0);
  thunar_search_options_add_rule (options, "[1]--", FALSE, FALSE);
  thunar_search_options_add_rule (options, "[not a regex] --flag", FALSE, TRUE);
  run_filtered (fixture, options, 1);
  thunar_search_options_free (options);
}

static void
set_modified (Fixture *fixture,
              const gchar *name,
              gint64 timestamp)
{
  GFile *file = g_file_get_child (fixture->directory, name);
  GError *error = NULL;
  g_assert_true (g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                             timestamp, G_FILE_QUERY_INFO_NONE, NULL, &error));
  g_assert_no_error (error);
  g_object_unref (file);
}

static void
test_size_and_dates (Fixture *fixture,
                     gconstpointer unused)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  GError *error = NULL;
  write_file (fixture, "small.txt", "123");
  write_file (fixture, "medium.txt", "12345");
  write_file (fixture, "large.txt", "123456789");
  options->min_size = 4;
  options->max_size = 8;
  run_filtered (fixture, options, 1);
  g_assert_true (g_hash_table_contains (fixture->paths, "medium.txt"));

  g_assert_true (thunar_search_options_set_dates (options, "2024-02-29", "2024-02-29", &error));
  g_assert_no_error (error);
  set_modified (fixture, "small.txt", options->modified_after - 1);
  set_modified (fixture, "medium.txt", options->modified_before - 1);
  set_modified (fixture, "large.txt", options->modified_before);
  run_filtered (fixture, options, 1); /* Size and date constraints are combined. */
  options->min_size = 0;
  options->max_size = G_MAXUINT64;
  run_filtered (fixture, options, 1);
  set_modified (fixture, "small.txt", options->modified_after);
  run_filtered (fixture, options, 2); /* Both boundaries include the chosen day. */
  thunar_search_options_free (options);
}

static void
test_invalid_filters (void)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  GError *error = NULL;
  options->min_size = 10;
  options->max_size = 9;
  g_assert_false (thunar_search_options_validate (options, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  options->max_size = G_MAXUINT64;
  g_free (options->name);
  options->name = g_strdup ("(");
  options->name_mode = THUNAR_SEARCH_NAME_REGEX;
  g_assert_false (thunar_search_options_validate (options, &error));
  g_assert_nonnull (error);
  g_clear_error (&error);
  g_assert_false (thunar_search_options_set_dates (options, "2023-02-29", "", &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  g_assert_false (thunar_search_options_set_dates (options, "2024-03-01", "2024-02-29", &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);

  /* Search jobs must own independent options even after the UI changes. */
  GValue value = G_VALUE_INIT;
  g_value_init (&value, THUNAR_TYPE_SEARCH_OPTIONS);
  g_value_set_boxed (&value, options);
  g_free (options->name);
  options->name = g_strdup ("changed");
  g_assert_cmpstr (((ThunarSearchOptions *) g_value_get_boxed (&value))->name, ==, "(");
  g_value_unset (&value);
  thunar_search_options_free (options);
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
  g_assert_true (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                  collect, fixture, &error));
  g_assert_no_error (error);

  write_file (fixture, "rg", "#!/bin/sh\nprintf './readable.txt\\0'\nexit 2\n");
  g_assert_cmpint (g_chmod (executable, 0700), ==, 0);
  g_assert_true (thunar_rg_search (fixture->directory, FALSE, fixture->cancellable,
                                  collect, fixture, &error));
  g_assert_no_error (error);
  g_assert_true (g_hash_table_contains (fixture->paths, "readable.txt"));

  write_file (fixture, "rg", "#!/bin/sh\nexit 3\n");
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

  /* Cancellation must also reap a later content-rule scan, after a completed
   * first pass has populated the intersection. */
  g_cancellable_reset (fixture->cancellable);
  write_file (fixture, "rg", "#!/bin/sh\nfor arg; do\n"
              "if [ \"$arg\" = second ]; then exec /bin/sleep 30; fi\ndone\n"
              "printf './readable.txt\\0'\n");
  g_assert_cmpint (g_chmod (executable, 0700), ==, 0);
  ThunarSearchOptions *options = thunar_search_options_new ();
  thunar_search_options_add_rule (options, "first", FALSE, TRUE);
  thunar_search_options_add_rule (options, "second", TRUE, TRUE);
  started = g_get_monotonic_time ();
  thread = g_thread_new ("cancel-search-rules", cancel_later, fixture->cancellable);
  g_assert_false (thunar_rg_search_full (fixture->directory, options, fixture->cancellable,
                                        collect, fixture, &error));
  g_thread_join (thread);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_cmpint (g_get_monotonic_time () - started, <, 2 * G_USEC_PER_SEC);
  g_clear_error (&error);
  thunar_search_options_free (options);

  g_free (executable);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/rg/files", Fixture, NULL, setup, test_files, teardown);
  g_test_add ("/rg/empty", Fixture, NULL, setup, test_empty, teardown);
  g_test_add ("/rg/directories", Fixture, NULL, setup, test_directories, teardown);
  g_test_add ("/rg/directory-cancellation", Fixture, NULL, setup, test_directory_cancellation, teardown);
  g_test_add ("/rg/inaccessible-paths", Fixture, NULL, setup, test_inaccessible_paths, teardown);
  g_test_add ("/rg/pre-cancelled", Fixture, NULL, setup, test_pre_cancelled, teardown);
  g_test_add ("/rg/process-errors", Fixture, NULL, setup, test_process_errors, teardown);
  g_test_add ("/rg/advanced/name-and-content", Fixture, NULL, setup, test_name_and_content, teardown);
  g_test_add ("/rg/advanced/rules", Fixture, NULL, setup, test_rules, teardown);
  g_test_add ("/rg/advanced/size-and-dates", Fixture, NULL, setup, test_size_and_dates, teardown);
  g_test_add_func ("/rg/advanced/invalid-filters", test_invalid_filters);
  return g_test_run ();
}
