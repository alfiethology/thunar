/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "thunar-rg-search.h"
#include <string.h>

G_DEFINE_BOXED_TYPE (ThunarSearchOptions, thunar_search_options,
                     thunar_search_options_copy, thunar_search_options_free)

ThunarSearchOptions *
thunar_search_options_new (void)
{
  ThunarSearchOptions *options = g_new0 (ThunarSearchOptions, 1);
  options->name = g_strdup ("");
  options->content = g_strdup ("");
  options->recursive = TRUE;
  options->max_size = G_MAXUINT64;
  options->modified_after = G_MININT64;
  options->modified_before = G_MAXINT64;
  return options;
}

ThunarSearchOptions *
thunar_search_options_copy (const ThunarSearchOptions *options)
{
  ThunarSearchOptions *copy;
  if (options == NULL)
    return NULL;
  copy = g_new (ThunarSearchOptions, 1);
  *copy = *options;
  copy->name = g_strdup (options->name);
  copy->content = g_strdup (options->content);
  return copy;
}

void
thunar_search_options_free (ThunarSearchOptions *options)
{
  if (options == NULL)
    return;
  g_free (options->name);
  g_free (options->content);
  g_free (options);
}

static GRegex *
thunar_search_name_regex (const ThunarSearchOptions *options,
                          GError                   **error)
{
  GRegex  *regex;
  GString *pattern;
  if (options->name_mode == THUNAR_SEARCH_NAME_CONTAINS || *options->name == '\0')
    return NULL;
  if (options->name_mode == THUNAR_SEARCH_NAME_REGEX)
    return g_regex_new (options->name, G_REGEX_OPTIMIZE | (options->case_sensitive ? 0 : G_REGEX_CASELESS),
                        0, error);

  pattern = g_string_new ("\\A");
  for (const gchar *p = options->name; *p != '\0'; ++p)
    {
      if (*p == '*')
        g_string_append (pattern, ".*");
      else if (*p == '?')
        g_string_append_c (pattern, '.');
      else
        {
          if (strchr ("\\.^$|()[]{}+", *p) != NULL)
            g_string_append_c (pattern, '\\');
          g_string_append_c (pattern, *p);
        }
    }
  g_string_append (pattern, "\\z");
  regex = g_regex_new (pattern->str, G_REGEX_OPTIMIZE | G_REGEX_DOTALL
                       | (options->case_sensitive ? 0 : G_REGEX_CASELESS), 0, error);
  g_string_free (pattern, TRUE);
  return regex;
}

gboolean
thunar_search_options_validate (const ThunarSearchOptions *options,
                                GError                   **error)
{
  GRegex *regex;
  if (options->min_size > options->max_size)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "Minimum file size must not exceed maximum file size");
      return FALSE;
    }
  if (options->modified_after >= options->modified_before)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "The start date must not be later than the end date");
      return FALSE;
    }
  if (options->name_mode != THUNAR_SEARCH_NAME_CONTAINS && *options->name != '\0')
    {
      regex = thunar_search_name_regex (options, error);
      if (regex == NULL)
        return FALSE;
      g_regex_unref (regex);
    }
  return TRUE;
}

static gboolean
thunar_search_parse_date (const gchar *text,
                          gboolean     end_of_day,
                          gint64      *timestamp,
                          GError     **error)
{
  GDateTime *date = NULL;
  GTimeZone *zone;
  gchar     *iso;
  if (*text == '\0')
    {
      *timestamp = end_of_day ? G_MAXINT64 : G_MININT64;
      return TRUE;
    }
  if (g_regex_match_simple ("^[0-9]{4}-[0-9]{2}-[0-9]{2}$", text, 0, 0))
    {
      iso = g_strconcat (text, "T00:00:00", NULL);
      zone = g_time_zone_new_local ();
      date = g_date_time_new_from_iso8601 (iso, zone);
      g_time_zone_unref (zone);
      g_free (iso);
    }
  if (date != NULL && end_of_day)
    {
      GDateTime *next = g_date_time_add_days (date, 1);
      g_date_time_unref (date);
      date = next;
    }
  if (date == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "Enter a valid date as YYYY-MM-DD, or leave the date empty");
      return FALSE;
    }
  *timestamp = g_date_time_to_unix (date);
  g_date_time_unref (date);
  return TRUE;
}

gboolean
thunar_search_options_set_dates (ThunarSearchOptions *options,
                                 const gchar         *after,
                                 const gchar         *before,
                                 GError             **error)
{
  gint64 start, end;
  if (!thunar_search_parse_date (after, FALSE, &start, error)
      || !thunar_search_parse_date (before, TRUE, &end, error))
    return FALSE;
  if (start >= end)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "The start date must not be later than the end date");
      return FALSE;
    }
  options->modified_after = start;
  options->modified_before = end;
  return TRUE;
}

static gboolean
thunar_search_matches (GFile                     *directory,
                       const gchar               *path,
                       const ThunarSearchOptions *options,
                       GRegex                    *regex,
                       gchar                    **terms,
                       GCancellable              *cancellable)
{
  gchar     *name;
  gchar     *folded;
  gboolean   matches = TRUE;
  GFile     *file;
  GFileInfo *info;

  /* Keep unfiltered filename enumeration on the basic search's fast path. */
  if (*options->name == '\0' && options->min_size == 0 && options->max_size == G_MAXUINT64
      && options->modified_after == G_MININT64 && options->modified_before == G_MAXINT64)
    return TRUE;

  name = g_filename_display_basename (path);
  if (regex != NULL)
    matches = g_regex_match (regex, name, 0, NULL);
  else
    {
      folded = options->case_sensitive ? g_strdup (name) : g_utf8_casefold (name, -1);
      for (guint i = 0; terms[i] != NULL; ++i)
        if (strstr (folded, terms[i]) == NULL)
          {
            matches = FALSE;
            break;
          }
      g_free (folded);
    }
  g_free (name);
  if (!matches)
    return FALSE;

  if (options->min_size == 0 && options->max_size == G_MAXUINT64
      && options->modified_after == G_MININT64 && options->modified_before == G_MAXINT64)
    return TRUE;

  file = g_file_resolve_relative_path (directory, path);
  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, NULL);
  g_object_unref (file);
  if (info == NULL)
    return FALSE;
  matches = (guint64) g_file_info_get_size (info) >= options->min_size
            && (guint64) g_file_info_get_size (info) <= options->max_size
            && (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED) >= options->modified_after
            && (gint64) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED) < options->modified_before;
  g_object_unref (info);
  return matches;
}



static void
thunar_rg_search_cancelled (GCancellable *cancellable,
                            gpointer      user_data)
{
  g_subprocess_force_exit (G_SUBPROCESS (user_data));
}



gboolean
thunar_rg_search (GFile              *directory,
                   gboolean            show_hidden,
                   GCancellable       *cancellable,
                   ThunarRgSearchFunc  callback,
                   gpointer            user_data,
                   GError            **error)
{
  ThunarSearchOptions *options = thunar_search_options_new ();
  gboolean            success;
  options->show_hidden = show_hidden;
  success = thunar_rg_search_full (directory, options, cancellable, callback, user_data, error);
  thunar_search_options_free (options);
  return success;
}

gboolean
thunar_rg_search_full (GFile                     *directory,
                       const ThunarSearchOptions *options,
                       GCancellable              *cancellable,
                       ThunarRgSearchFunc         callback,
                       gpointer                   user_data,
                       GError                   **error)
{
  GSubprocessLauncher *launcher;
  GSubprocess         *process;
  GDataInputStream    *stream;
  GError             *err = NULL;
  gchar              *directory_path;
  gchar              *path;
  gulong              cancel_id;
  const gchar        *argv[20];
  guint               argc = 0;
  GRegex             *regex;
  gchar             **terms;
  gchar              *name;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;
  if (!thunar_search_options_validate (options, error))
    return FALSE;

  argv[argc++] = "rg";
  argv[argc++] = "--no-config";
  argv[argc++] = *options->content != '\0' ? "--files-with-matches" : "--files";
  argv[argc++] = "--null";
  argv[argc++] = "--no-ignore";
  argv[argc++] = "--color=never";
  argv[argc++] = "--no-messages";
  argv[argc++] = options->show_hidden ? "--hidden" : "--glob=!*~";
  if (!options->recursive)
    argv[argc++] = "--max-depth=1";
  if (*options->content != '\0')
    {
      argv[argc++] = options->case_sensitive ? "--case-sensitive" : "--ignore-case";
      argv[argc++] = "--fixed-strings";
      argv[argc++] = "--regexp";
      argv[argc++] = options->content;
    }
  argv[argc++] = "--";
  argv[argc++] = ".";
  argv[argc] = NULL;

  directory_path = g_file_get_path (directory);
  if (directory_path == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "ripgrep requires a local search directory");
      return FALSE;
    }

  /* Only advanced searches with content text read file contents. All modes
   * stream NUL-delimited filenames without a shell or user rg configuration. */
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                      | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_subprocess_launcher_set_cwd (launcher, directory_path);
  g_free (directory_path);
  process = g_subprocess_launcher_spawnv (launcher, argv, error);
  g_object_unref (launcher);
  if (process == NULL)
    return FALSE;

  regex = thunar_search_name_regex (options, NULL);
  name = options->case_sensitive ? g_strdup (options->name) : g_utf8_casefold (options->name, -1);
  terms = g_regex_split_simple ("\\s+", name, 0, 0);
  g_free (name);

  cancel_id = g_cancellable_connect (cancellable,
                                    G_CALLBACK (thunar_rg_search_cancelled),
                                    process, NULL);
  stream = g_data_input_stream_new (g_subprocess_get_stdout_pipe (process));

  /* NUL delimiters also handle filenames containing newlines. read_upto()
   * leaves the delimiter in the stream, so consume it explicitly. */
  while ((path = g_data_input_stream_read_upto (stream, "\0", 1, NULL,
                                               cancellable, &err)) != NULL)
    {
      g_data_input_stream_read_byte (stream, cancellable, &err);
      if (err == NULL && !g_cancellable_is_cancelled (cancellable)
          && thunar_search_matches (directory, path, options, regex, terms, cancellable))
        callback (path, user_data);
      g_free (path);
      if (err != NULL)
        break;
    }

  if (err != NULL)
    g_subprocess_force_exit (process);

  /* Reap the child even after cancellation or a read error. */
  g_subprocess_wait (process, NULL, NULL);
  g_cancellable_disconnect (cancellable, cancel_id);

  if (err == NULL && !g_cancellable_set_error_if_cancelled (cancellable, &err))
    {
      /* rg uses status 1 for an empty file list. */
      if (!g_subprocess_get_if_exited (process)
          || g_subprocess_get_exit_status (process) > 1)
        g_set_error_literal (&err, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "ripgrep could not search all files in this directory");
    }

  g_object_unref (stream);
  g_object_unref (process);
  if (regex != NULL)
    g_regex_unref (regex);
  g_strfreev (terms);
  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  return TRUE;
}
