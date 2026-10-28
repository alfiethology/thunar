/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "thunar-rg-search.h"
#include <string.h>

G_DEFINE_BOXED_TYPE (ThunarSearchOptions, thunar_search_options,
                     thunar_search_options_copy, thunar_search_options_free)

static void
thunar_search_rule_free (gpointer data)
{
  ThunarSearchRule *rule = data;
  g_free (rule->text);
  g_free (rule);
}

void
thunar_search_options_add_rule (ThunarSearchOptions *options, const gchar *text,
                                gboolean exclude, gboolean in_contents)
{
  ThunarSearchRule *rule = g_new0 (ThunarSearchRule, 1);
  rule->text = g_strdup (text);
  rule->exclude = exclude;
  rule->in_contents = in_contents;
  g_ptr_array_add (options->rules, rule);
}

void
thunar_search_options_add_directory (ThunarSearchOptions *options, const gchar *path, gboolean exclude)
{
  thunar_search_options_add_rule (options, path, exclude, FALSE);
  ((ThunarSearchRule *) g_ptr_array_index (options->rules, options->rules->len - 1))->is_directory = TRUE;
}

ThunarSearchOptions *
thunar_search_options_new (void)
{
  ThunarSearchOptions *options = g_new0 (ThunarSearchOptions, 1);
  options->name = g_strdup ("");
  options->content = g_strdup ("");
  options->rules = g_ptr_array_new_with_free_func (thunar_search_rule_free);
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
  copy->rules = g_ptr_array_new_with_free_func (thunar_search_rule_free);
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      thunar_search_options_add_rule (copy, rule->text, rule->exclude, rule->in_contents);
      ((ThunarSearchRule *) g_ptr_array_index (copy->rules, i))->is_directory = rule->is_directory;
    }
  return copy;
}

void
thunar_search_options_free (ThunarSearchOptions *options)
{
  if (options == NULL)
    return;
  g_free (options->name);
  g_free (options->content);
  g_ptr_array_unref (options->rules);
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
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      if (rule->is_directory && !g_path_is_absolute (rule->text))
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                               "Directory rules require an absolute folder path");
          return FALSE;
        }
    }
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
  if (*options->name == '\0' && options->rules->len == 0 && options->min_size == 0 && options->max_size == G_MAXUINT64
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
  for (guint i = 0; matches && i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      if (!rule->is_directory && !rule->in_contents && *rule->text != '\0')
        {
          gchar *haystack = options->case_sensitive ? g_strdup (name) : g_utf8_casefold (name, -1);
          gchar *needle = options->case_sensitive ? g_strdup (rule->text) : g_utf8_casefold (rule->text, -1);
          matches = (strstr (haystack, needle) != NULL) != rule->exclude;
          g_free (needle);
          g_free (haystack);
        }
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

static gboolean
thunar_search_path_excluded (GFile *file, const ThunarSearchOptions *options)
{
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      if (rule->is_directory && rule->exclude)
        {
          GFile *excluded = g_file_new_for_path (rule->text);
          gboolean matches = g_file_equal (file, excluded) || g_file_has_prefix (file, excluded);
          g_object_unref (excluded);
          if (matches)
            return TRUE;
        }
    }
  return FALSE;
}

static gchar *
thunar_search_exclusion_glob (const gchar *relative)
{
  GString *glob = g_string_new ("!/");
  for (const gchar *p = relative; *p != '\0'; ++p)
    {
      if (strchr ("\\*?[]{}", *p) != NULL)
        g_string_append_c (glob, '\\');
      g_string_append_c (glob, *p);
    }
  return g_string_free (glob, FALSE);
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

static gboolean
thunar_rg_search_single (GFile                     *directory,
                       const ThunarSearchOptions *options,
                       gboolean                   exclude_content,
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
  argv[argc++] = *options->content != '\0'
                  ? (exclude_content ? "--files-without-match" : "--files-with-matches") : "--files";
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
  GPtrArray *arguments = g_ptr_array_new_with_free_func (g_free);
  for (guint i = 0; i < argc; ++i)
    g_ptr_array_add (arguments, g_strdup (argv[i]));
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      if (rule->is_directory && rule->exclude)
        {
          GFile *excluded = g_file_new_for_path (rule->text);
          gchar *relative = g_file_get_relative_path (directory, excluded);
          if (relative != NULL)
            {
              g_ptr_array_add (arguments, g_strdup ("--glob"));
              g_ptr_array_add (arguments, thunar_search_exclusion_glob (relative));
              g_free (relative);
            }
          g_object_unref (excluded);
        }
    }
  g_ptr_array_add (arguments, g_strdup ("--"));
  g_ptr_array_add (arguments, g_strdup ("."));
  g_ptr_array_add (arguments, NULL);

  directory_path = g_file_get_path (directory);
  if (directory_path == NULL)
    {
      g_ptr_array_unref (arguments);
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
  process = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) arguments->pdata, error);
  g_ptr_array_unref (arguments);
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
      /* rg uses status 1 for no matches and status 2 when some paths could
       * not be read. Keep the accessible results without showing a dialog
       * for expected permission errors during recursive searches. Spawn,
       * stream and cancellation errors are still reported separately. */
      if (!g_subprocess_get_if_exited (process)
          || g_subprocess_get_exit_status (process) > 2)
        g_set_error_literal (&err, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "ripgrep could not complete the search");
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

typedef struct
{
  GHashTable *previous;
  GHashTable *next;
  ThunarRgSearchFunc callback;
  gpointer user_data;
} ThunarSearchIntersection;

static gboolean
thunar_search_directories (GFile *directory, const ThunarSearchOptions *options,
                           GCancellable *cancellable, ThunarRgSearchFunc callback,
                           gpointer user_data, GError **error)
{
  GQueue pending = G_QUEUE_INIT;
  GRegex *regex = thunar_search_name_regex (options, NULL);
  gchar *name = options->case_sensitive ? g_strdup (options->name) : g_utf8_casefold (options->name, -1);
  gchar **terms = g_regex_split_simple ("\\s+", name, 0, 0);
  g_free (name);
  g_queue_push_tail (&pending, g_strdup ("."));
  /* rg --files does not expose directories, including empty ones. Enumerate
   * only names and types for this extra pass: no per-file metadata queries,
   * symlink traversal, or file-content reads. This runs in the search worker. */
  while (!g_queue_is_empty (&pending) && !g_cancellable_is_cancelled (cancellable))
    {
      gchar *relative = g_queue_pop_head (&pending);
      GFile *parent = g_file_resolve_relative_path (directory, relative);
      GFileEnumerator *entries = g_file_enumerate_children (parent,
          G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, NULL);
      GFileInfo *info;
      g_object_unref (parent);
      if (entries != NULL)
        {
          while (!g_cancellable_is_cancelled (cancellable)
                 && (info = g_file_enumerator_next_file (entries, cancellable, NULL)) != NULL)
            {
              const gchar *child_name = g_file_info_get_name (info);
              if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY
                  && (options->show_hidden || (child_name[0] != '.' && !g_str_has_suffix (child_name, "~"))))
                {
                  gchar *path = g_build_filename (relative, child_name, NULL);
                  GFile *child = g_file_resolve_relative_path (directory, path);
                  gboolean excluded = thunar_search_path_excluded (child, options);
                  g_object_unref (child);
                  if (excluded)
                    {
                      g_free (path);
                      g_object_unref (info);
                      continue;
                    }
                  if (thunar_search_matches (directory, path, options, regex, terms, cancellable))
                    callback (path, user_data);
                  if (options->recursive)
                    g_queue_push_tail (&pending, path);
                  else
                    g_free (path);
                }
              g_object_unref (info);
            }
          g_object_unref (entries);
        }
      g_free (relative);
    }
  g_queue_clear_full (&pending, g_free);
  if (regex != NULL)
    g_regex_unref (regex);
  g_strfreev (terms);
  return !g_cancellable_set_error_if_cancelled (cancellable, error);
}

static void
thunar_search_intersect (const gchar *path, gpointer user_data)
{
  ThunarSearchIntersection *intersection = user_data;
  if (intersection->previous == NULL || g_hash_table_contains (intersection->previous, path))
    {
      if (intersection->next != NULL)
        g_hash_table_add (intersection->next, g_strdup (path));
      else
        intersection->callback (path, intersection->user_data);
    }
}

static gboolean
thunar_rg_search_root (GFile *directory, const ThunarSearchOptions *options,
                       GCancellable *cancellable, ThunarRgSearchFunc callback,
                       gpointer user_data, GError **error)
{
  GPtrArray *contents;
  ThunarSearchRule legacy = { options->content, FALSE, TRUE, FALSE };
  ThunarSearchIntersection intersection = { NULL, NULL, callback, user_data };
  gboolean success = TRUE;
  if (!thunar_search_options_validate (options, error))
    return FALSE;
  contents = g_ptr_array_new ();
  if (*options->content != '\0')
    g_ptr_array_add (contents, &legacy);
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      if (!rule->is_directory && rule->in_contents && *rule->text != '\0')
        g_ptr_array_add (contents, rule);
    }
  /* Filename-only searches keep the streaming rg --files fast path. Content
   * rules use one bounded-memory scan each, intersecting file sets rather than
   * launching a process for every file. Exclusions select successfully read
   * nonmatching files, so unreadable files cannot falsely pass an exclusion. */
  if (contents->len == 0)
    {
      success = thunar_rg_search_single (directory, options, FALSE, cancellable, callback, user_data, error);
      if (success && !options->exclude_directories)
        success = thunar_search_directories (directory, options, cancellable, callback, user_data, error);
    }
  for (guint i = 0; success && i < contents->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (contents, i);
      ThunarSearchOptions pass = *options;
      pass.content = rule->text;
      intersection.next = i + 1 < contents->len ? g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL) : NULL;
      success = thunar_rg_search_single (directory, &pass, rule->exclude, cancellable,
                                          thunar_search_intersect, &intersection, error);
      g_clear_pointer (&intersection.previous, g_hash_table_unref);
      intersection.previous = intersection.next;
      if (success && intersection.previous != NULL && g_hash_table_size (intersection.previous) == 0)
        break;
    }
  g_clear_pointer (&intersection.previous, g_hash_table_unref);
  g_ptr_array_unref (contents);
  return success;
}

typedef struct
{
  GFile *base;
  GFile *root;
  const ThunarSearchOptions *options;
  GHashTable *seen;
  ThunarRgSearchFunc callback;
  gpointer user_data;
} ThunarSearchScope;

static void
thunar_search_scope_result (const gchar *path, gpointer user_data)
{
  ThunarSearchScope *scope = user_data;
  GFile *file = g_file_resolve_relative_path (scope->root, path);
  if (!thunar_search_path_excluded (file, scope->options) && !g_hash_table_contains (scope->seen, file))
    {
      gchar *relative = g_file_get_relative_path (scope->base, file);
      gchar *result = relative != NULL ? g_strconcat ("./", relative, NULL) : g_file_get_path (file);
      g_hash_table_add (scope->seen, g_object_ref (file));
      scope->callback (result, scope->user_data);
      g_free (relative);
      g_free (result);
    }
  g_object_unref (file);
}

gboolean
thunar_rg_search_full (GFile *directory, const ThunarSearchOptions *options,
                       GCancellable *cancellable, ThunarRgSearchFunc callback,
                       gpointer user_data, GError **error)
{
  GPtrArray *roots = g_ptr_array_new_with_free_func (g_object_unref);
  gboolean scoped = FALSE;
  gboolean success = TRUE;
  if (!thunar_search_options_validate (options, error))
    {
      g_ptr_array_unref (roots);
      return FALSE;
    }
  g_ptr_array_add (roots, g_object_ref (directory));
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      if (rule->is_directory)
        {
          scoped = TRUE;
          if (!rule->exclude)
            {
              GFile *included = g_file_new_for_path (rule->text);
              gboolean duplicate = FALSE;
              for (guint j = 0; j < roots->len; ++j)
                if (g_file_equal (included, g_ptr_array_index (roots, j)))
                  duplicate = TRUE;
              if (duplicate)
                g_object_unref (included);
              else
                g_ptr_array_add (roots, included);
            }
        }
    }
  if (!scoped)
    success = thunar_rg_search_root (directory, options, cancellable, callback, user_data, error);
  else
    {
      ThunarSearchScope scope = { directory, NULL, options,
          g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, g_object_unref, NULL), callback, user_data };
      for (guint i = 0; success && i < roots->len; ++i)
        {
          scope.root = g_ptr_array_index (roots, i);
          if (g_cancellable_set_error_if_cancelled (cancellable, error))
            success = FALSE;
          else if (!thunar_search_path_excluded (scope.root, options))
            success = thunar_rg_search_root (scope.root, options, cancellable,
                                             thunar_search_scope_result, &scope, error);
        }
      g_hash_table_unref (scope.seen);
    }
  g_ptr_array_unref (roots);
  return success;
}
