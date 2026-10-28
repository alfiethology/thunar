/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <glib/gi18n.h>
#include <string.h>
#include <thunar/thunar-advanced-search-dialog.h>

typedef struct
{
  GtkWidget *directory;
  GtkWidget *name;
  GtkWidget *name_mode;
  GtkWidget *rules;
  GtkWidget *min_size;
  GtkWidget *max_size;
  GtkWidget *after;
  GtkWidget *before;
  GtkWidget *match_case;
  GtkWidget *recursive;
  GtkWidget *hidden;
  GtkWidget *exclude_directories;
} SearchFields;

typedef struct
{
  GtkWidget *action;
  GtkWidget *target;
  GtkWidget *text;
} RuleFields;

typedef struct
{
  SearchFields *fields;
  GtkEntryCompletion *completion;
  GCancellable *cancel;
  guint timeout;
  gboolean directory;
  gboolean destroyed;
} DirectoryCompletion;

typedef struct
{
  GFile *base;
  gchar *text;
} DirectoryQuery;

static void
directory_query_free (gpointer data)
{
  DirectoryQuery *query = data;
  g_clear_object (&query->base);
  g_free (query->text);
  g_free (query);
}

static void
directory_suggestions (GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
  DirectoryQuery *query = task_data;
  GPtrArray *results = g_ptr_array_new_with_free_func (g_free);
  GHashTable *visited = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GQueue pending = G_QUEUE_INIT;
  gboolean path_mode = strchr (query->text, G_DIR_SEPARATOR) != NULL || query->text[0] == '~';
  gchar *needle;
  if (path_mode)
    {
      gchar *expanded;
      if (g_str_equal (query->text, "~"))
        expanded = g_strdup (g_get_home_dir ());
      else if (g_str_has_prefix (query->text, "~/"))
        expanded = g_build_filename (g_get_home_dir (), query->text + 2, NULL);
      else if (g_path_is_absolute (query->text))
        expanded = g_strdup (query->text);
      else
        {
          GFile *file = g_file_resolve_relative_path (query->base, query->text);
          expanded = g_file_get_path (file);
          g_object_unref (file);
        }
      gboolean inside = g_str_has_suffix (query->text, G_DIR_SEPARATOR_S);
      gchar *leaf = inside ? g_strdup ("") : g_path_get_basename (expanded);
      needle = g_utf8_casefold (leaf, -1);
      g_free (leaf);
      g_queue_push_tail (&pending, inside ? g_strdup (expanded) : g_path_get_dirname (expanded));
      g_free (expanded);
    }
  else
    {
      needle = g_utf8_casefold (query->text, -1);
      g_queue_push_tail (&pending, g_file_get_path (query->base));
      g_queue_push_tail (&pending, g_strdup (g_get_home_dir ()));
    }
  /* Suggestions are intentionally bounded; full paths also work for folders
   * outside the current folder/Home, without indexing the whole computer. */
  gint64 deadline = g_get_monotonic_time () + G_USEC_PER_SEC;
  guint scanned = 0;
  while (!g_queue_is_empty (&pending) && results->len < 100 && scanned < 10000
         && g_get_monotonic_time () < deadline && !g_cancellable_is_cancelled (cancel))
    {
      gchar *path = g_queue_pop_head (&pending);
      if (path == NULL || g_hash_table_contains (visited, path))
        {
          g_free (path);
          continue;
        }
      g_hash_table_add (visited, g_strdup (path));
      ++scanned;
      GFile *folder = g_file_new_for_path (path);
      GFileEnumerator *entries = g_file_enumerate_children (folder,
          G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, NULL);
      GFileInfo *info;
      if (entries != NULL)
        {
          while (results->len < 100 && g_get_monotonic_time () < deadline
                 && !g_cancellable_is_cancelled (cancel)
                 && (info = g_file_enumerator_next_file (entries, cancel, NULL)) != NULL)
            {
              const gchar *name = g_file_info_get_name (info);
              if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY
                  && (name[0] != '.' || needle[0] == '.'))
                {
                  gchar *display = g_filename_display_name (name);
                  gchar *folded = g_utf8_casefold (display, -1);
                  gchar *child = g_build_filename (path, name, NULL);
                  if (strstr (folded, needle) != NULL)
                    {
                      /* Don't suggest the same folder twice for overlapping roots. */
                      gboolean duplicate = FALSE;
                      for (guint i = 0; i < results->len; ++i)
                        if (g_str_equal (g_ptr_array_index (results, i), child))
                          duplicate = TRUE;
                      if (!duplicate)
                        g_ptr_array_add (results, g_strdup (child));
                    }
                  if (!path_mode)
                    g_queue_push_tail (&pending, child);
                  else
                    g_free (child);
                  g_free (folded);
                  g_free (display);
                }
              g_object_unref (info);
            }
          g_object_unref (entries);
        }
      g_object_unref (folder);
      g_free (path);
    }
  g_queue_clear_full (&pending, g_free);
  g_hash_table_unref (visited);
  g_free (needle);
  if (g_task_return_error_if_cancelled (task))
    g_ptr_array_unref (results);
  else
    g_task_return_pointer (task, results, (GDestroyNotify) g_ptr_array_unref);
}

static void
directory_suggestions_ready (GObject *source, GAsyncResult *result, gpointer unused)
{
  DirectoryCompletion *state = g_object_get_data (source, "directory-completion");
  GError *error = NULL;
  GPtrArray *paths = g_task_propagate_pointer (G_TASK (result), &error);
  DirectoryQuery *query = g_task_get_task_data (G_TASK (result));
  if (paths != NULL && !state->destroyed && state->directory
      && state->cancel == g_task_get_cancellable (G_TASK (result))
      && g_str_equal (query->text, gtk_entry_get_text (GTK_ENTRY (source))))
    {
      GtkListStore *model = GTK_LIST_STORE (gtk_entry_completion_get_model (state->completion));
      gtk_list_store_clear (model);
      for (guint i = 0; i < paths->len; ++i)
        {
          GtkTreeIter iter;
          gtk_list_store_append (model, &iter);
          gtk_list_store_set (model, &iter, 0, g_ptr_array_index (paths, i), -1);
        }
      if (gtk_widget_has_focus (GTK_WIDGET (source)))
        gtk_entry_completion_complete (state->completion);
    }
  g_clear_pointer (&paths, g_ptr_array_unref);
  g_clear_error (&error);
}

static gboolean
start_directory_suggestions (gpointer data)
{
  GtkEntry *entry = data;
  DirectoryCompletion *state = g_object_get_data (G_OBJECT (entry), "directory-completion");
  state->timeout = 0;
  if (state->destroyed || !state->directory)
    return G_SOURCE_REMOVE;
  DirectoryQuery *query = g_new0 (DirectoryQuery, 1);
  query->text = g_strdup (gtk_entry_get_text (entry));
  query->base = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (state->fields->directory));
  if (query->base == NULL)
    query->base = g_file_new_for_path (g_get_home_dir ());
  g_clear_object (&state->cancel);
  state->cancel = g_cancellable_new ();
  GTask *task = g_task_new (entry, state->cancel, directory_suggestions_ready, NULL);
  g_task_set_task_data (task, query, directory_query_free);
  g_task_run_in_thread (task, directory_suggestions);
  g_object_unref (task);
  return G_SOURCE_REMOVE;
}

static void
cancel_directory_suggestions (DirectoryCompletion *state)
{
  if (state->timeout != 0)
    {
      g_source_remove (state->timeout);
      state->timeout = 0;
    }
  if (state->cancel != NULL)
    g_cancellable_cancel (state->cancel);
}

static void
directory_entry_changed (GtkEntry *entry, gpointer unused)
{
  DirectoryCompletion *state = g_object_get_data (G_OBJECT (entry), "directory-completion");
  cancel_directory_suggestions (state);
  gtk_list_store_clear (GTK_LIST_STORE (gtk_entry_completion_get_model (state->completion)));
  if (!state->destroyed && state->directory && *gtk_entry_get_text (entry) != '\0')
    state->timeout = g_timeout_add (200, start_directory_suggestions, entry);
}

static void
directory_entry_destroyed (GtkWidget *entry, gpointer unused)
{
  DirectoryCompletion *state = g_object_get_data (G_OBJECT (entry), "directory-completion");
  state->destroyed = TRUE;
  cancel_directory_suggestions (state);
}

static void
directory_completion_free (gpointer data)
{
  DirectoryCompletion *state = data;
  cancel_directory_suggestions (state);
  g_clear_object (&state->cancel);
  g_object_unref (state->completion);
  g_free (state);
}

static gboolean
directory_completion_match (GtkEntryCompletion *completion, const gchar *key, GtkTreeIter *iter, gpointer data)
{
  return TRUE; /* The worker already matched basename/path queries. */
}

static gboolean
directory_selected (GtkEntryCompletion *completion, GtkTreeModel *model, GtkTreeIter *iter, GtkEntry *entry)
{
  gchar *path;
  gtk_tree_model_get (model, iter, 0, &path, -1);
  gtk_entry_set_text (entry, path);
  g_free (path);
  /* Selection is complete; don't reopen the same suggestion on another timer. */
  cancel_directory_suggestions (g_object_get_data (G_OBJECT (entry), "directory-completion"));
  return TRUE;
}

static void
rule_target_changed (GtkComboBox *target, GtkEntry *entry)
{
  DirectoryCompletion *state = g_object_get_data (G_OBJECT (entry), "directory-completion");
  state->directory = gtk_combo_box_get_active (target) == 2;
  gtk_entry_set_completion (entry, state->directory ? state->completion : NULL);
  gtk_entry_set_placeholder_text (entry, state->directory ? _("Type a folder name or path") : _("Text to match (literal)"));
  gtk_widget_set_tooltip_text (GTK_WIDGET (entry), state->directory
      ? _("Choose a folder from suggestions in Search in/Home, or enter its full path") : NULL);
  directory_entry_changed (entry, NULL);
}

static GtkWidget *
add_rule (SearchFields *fields, const gchar *text, gboolean exclude, gint target)
{
  RuleFields *rule = g_new0 (RuleFields, 1);
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *remove = gtk_button_new_from_icon_name ("list-remove-symbolic", GTK_ICON_SIZE_BUTTON);
  g_object_set_data_full (G_OBJECT (row), "search-rule", rule, g_free);
  rule->action = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (rule->action), _("Include"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (rule->action), _("Exclude"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (rule->action), exclude ? 1 : 0);
  rule->target = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (rule->target), _("Filename"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (rule->target), _("File contents"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (rule->target), _("Directory"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (rule->target), target);
  rule->text = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (rule->text), text);
  gtk_entry_set_placeholder_text (GTK_ENTRY (rule->text), _("Text to match (literal)"));
  gtk_entry_set_activates_default (GTK_ENTRY (rule->text), TRUE);
  DirectoryCompletion *completion = g_new0 (DirectoryCompletion, 1);
  completion->fields = fields;
  completion->completion = gtk_entry_completion_new ();
  GtkListStore *suggestions = gtk_list_store_new (1, G_TYPE_STRING);
  gtk_entry_completion_set_model (completion->completion, GTK_TREE_MODEL (suggestions));
  g_object_unref (suggestions);
  gtk_entry_completion_set_text_column (completion->completion, 0);
  gtk_entry_completion_set_match_func (completion->completion, directory_completion_match, NULL, NULL);
  gtk_entry_completion_set_minimum_key_length (completion->completion, 1);
  g_object_set_data_full (G_OBJECT (rule->text), "directory-completion", completion, directory_completion_free);
  g_signal_connect_object (completion->completion, "match-selected", G_CALLBACK (directory_selected), rule->text, 0);
  g_signal_connect (rule->text, "changed", G_CALLBACK (directory_entry_changed), NULL);
  g_signal_connect (rule->text, "destroy", G_CALLBACK (directory_entry_destroyed), NULL);
  g_signal_connect_object (rule->target, "changed", G_CALLBACK (rule_target_changed), rule->text, 0);
  rule_target_changed (GTK_COMBO_BOX (rule->target), GTK_ENTRY (rule->text));
  gtk_widget_set_name (rule->action, "search-rule-action");
  gtk_widget_set_name (rule->target, "search-rule-target");
  gtk_widget_set_name (rule->text, "search-rule-text");
  gtk_widget_set_name (remove, "remove-search-rule");
  gtk_widget_set_tooltip_text (remove, _("Remove this rule"));
  g_signal_connect_swapped (remove, "clicked", G_CALLBACK (gtk_widget_destroy), row);
  gtk_box_pack_start (GTK_BOX (row), rule->action, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (row), rule->target, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (row), rule->text, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (row), remove, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (fields->rules), row, FALSE, FALSE, 0);
  gtk_widget_show_all (row);
  return rule->text;
}

static void
add_rule_clicked (GtkButton *button, SearchFields *fields)
{
  gtk_widget_grab_focus (add_rule (fields, "", FALSE, FALSE));
}

static void
add_field (GtkWidget   *grid,
           const gchar *text,
           GtkWidget   *widget,
           gint         row)
{
  GtkWidget *label = gtk_label_new_with_mnemonic (text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
  gtk_widget_set_hexpand (widget, TRUE);
  gtk_grid_attach (GTK_GRID (grid), widget, 1, row, 1, 1);
}

static GtkWidget *
new_entry (const gchar *text,
           const gchar *placeholder)
{
  GtkWidget *entry = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (entry), text);
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), placeholder);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  return entry;
}

static void
set_date (GtkWidget *entry,
          gint64     timestamp)
{
  GDateTime *date = g_date_time_new_from_unix_local (timestamp);
  if (date != NULL)
    {
      gchar *text = g_date_time_format (date, "%Y-%m-%d");
      gtk_entry_set_text (GTK_ENTRY (entry), text);
      g_free (text);
      g_date_time_unref (date);
    }
}

GtkWidget *
thunar_advanced_search_dialog_new (GtkWindow                 *parent,
                                   GFile                     *directory,
                                   const ThunarSearchOptions *options)
{
  SearchFields *fields = g_new0 (SearchFields, 1);
  GtkWidget    *dialog;
  GtkWidget    *grid;
  GtkWidget    *help;

  dialog = gtk_dialog_new_with_buttons (_("Advanced Search"), parent,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        _("_Search"), GTK_RESPONSE_ACCEPT, NULL);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 700, -1);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  g_object_set_data_full (G_OBJECT (dialog), "search-fields", fields, g_free);
  grid = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid), 18);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 10);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 16);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), grid, TRUE, TRUE, 0);

  fields->directory = gtk_file_chooser_button_new (_("Search in Folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (fields->directory), TRUE);
  gtk_file_chooser_set_file (GTK_FILE_CHOOSER (fields->directory), directory, NULL);
  add_field (grid, _("Search _in:"), fields->directory, 0);

  fields->name = new_entry (options->name, _("Leave empty for any filename"));
  add_field (grid, _("_Filename filter:"), fields->name, 1);
  fields->name_mode = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (fields->name_mode), _("Contains all words"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (fields->name_mode), _("Wildcards (* and ?)"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (fields->name_mode), _("Regular expression"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (fields->name_mode), options->name_mode);
  add_field (grid, _("Filename _matching:"), fields->name_mode, 2);

  GtkWidget *rule_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
  GtkWidget *add = gtk_button_new_with_mnemonic (_("_Add rule"));
  gtk_widget_set_name (add, "add-search-rule");
  gtk_widget_set_halign (add, GTK_ALIGN_START);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroll), 100);
  gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scroll), 190);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scroll), TRUE);
  fields->rules = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (scroll), fields->rules);
  gtk_box_pack_start (GTK_BOX (rule_box), scroll, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (rule_box), add, FALSE, FALSE, 0);
  g_signal_connect (add, "clicked", G_CALLBACK (add_rule_clicked), fields);
  add_field (grid, _("_Rules:"), rule_box, 3);
  if (*options->content != '\0')
    add_rule (fields, options->content, FALSE, TRUE);
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      add_rule (fields, rule->text, rule->exclude, rule->is_directory ? 2 : rule->in_contents ? 1 : 0);
    }
  if (options->rules->len == 0 && *options->content == '\0')
    add_rule (fields, "", FALSE, FALSE);
  fields->min_size = gtk_spin_button_new_with_range (0, 1000000000000.0, 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fields->min_size), options->min_size / 1024.0);
  add_field (grid, _("Minimum si_ze (KiB):"), fields->min_size, 4);
  fields->max_size = gtk_spin_button_new_with_range (0, 1000000000000.0, 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fields->max_size), options->max_size == G_MAXUINT64 ? 0 : options->max_size / 1024.0);
  gtk_widget_set_tooltip_text (fields->max_size, _("0 means no maximum size"));
  add_field (grid, _("Ma_ximum size (KiB):"), fields->max_size, 5);
  fields->after = new_entry ("", _("YYYY-MM-DD, or empty"));
  fields->before = new_entry ("", _("YYYY-MM-DD, or empty"));
  if (options->modified_after != G_MININT64)
    set_date (fields->after, options->modified_after);
  if (options->modified_before != G_MAXINT64)
    set_date (fields->before, options->modified_before - 1);
  add_field (grid, _("Modified _from:"), fields->after, 6);
  add_field (grid, _("Modified _through:"), fields->before, 7);

  fields->match_case = gtk_check_button_new_with_mnemonic (_("Match _case in filenames and contents"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fields->match_case), options->case_sensitive);
  gtk_grid_attach (GTK_GRID (grid), fields->match_case, 0, 8, 2, 1);
  fields->recursive = gtk_check_button_new_with_mnemonic (_("Search _subfolders"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fields->recursive), options->recursive);
  gtk_grid_attach (GTK_GRID (grid), fields->recursive, 0, 9, 2, 1);
  fields->hidden = gtk_check_button_new_with_mnemonic (_("Include _hidden and backup files"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fields->hidden), options->show_hidden);
  gtk_grid_attach (GTK_GRID (grid), fields->hidden, 0, 10, 2, 1);
  fields->exclude_directories = gtk_check_button_new_with_mnemonic (_("Exclude _directories"));
  gtk_widget_set_name (fields->exclude_directories, "exclude-directories");
  gtk_widget_set_tooltip_text (fields->exclude_directories, _("Hide directories from results; files inside them are still searched"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fields->exclude_directories), options->exclude_directories);
  gtk_grid_attach (GTK_GRID (grid), fields->exclude_directories, 0, 11, 2, 1);

  help = gtk_label_new (_("Filename/content includes must all match; excluded text must not occur.\n"
                         "Directory includes add search folders; excludes skip entire subfolders.\n"
                         "Empty rules are ignored. Content rules return files only.\n"
                         "Maximum size 0 means unlimited. Dates include the whole day."));
  gtk_label_set_xalign (GTK_LABEL (help), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (help), TRUE);
  gtk_style_context_add_class (gtk_widget_get_style_context (help), "dim-label");
  gtk_grid_attach (GTK_GRID (grid), help, 0, 12, 2, 1);
  gtk_widget_show_all (grid);
  gtk_widget_grab_focus (fields->name);
  return dialog;
}

ThunarSearchOptions *
thunar_advanced_search_dialog_get_options (GtkWidget *dialog,
                                           GError   **error)
{
  SearchFields *fields = g_object_get_data (G_OBJECT (dialog), "search-fields");
  ThunarSearchOptions *options = thunar_search_options_new ();
  g_free (options->name);
  options->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (fields->name)));
  GList *rows = gtk_container_get_children (GTK_CONTAINER (fields->rules));
  for (GList *lp = rows; lp != NULL; lp = lp->next)
    {
      RuleFields *rule = g_object_get_data (G_OBJECT (lp->data), "search-rule");
      const gchar *text = gtk_entry_get_text (GTK_ENTRY (rule->text));
      if (*text != '\0')
        {
          if (gtk_combo_box_get_active (GTK_COMBO_BOX (rule->target)) == 2)
            {
              GFile *base = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (fields->directory));
              GFile *folder;
              if (base == NULL)
                base = g_file_new_for_path (g_get_home_dir ());
              if (g_str_has_prefix (text, "~/"))
                {
                  gchar *expanded = g_build_filename (g_get_home_dir (), text + 2, NULL);
                  folder = g_file_new_for_path (expanded);
                  g_free (expanded);
                }
              else if (g_str_equal (text, "~"))
                folder = g_file_new_for_path (g_get_home_dir ());
              else if (g_path_is_absolute (text))
                folder = g_file_new_for_path (text);
              else
                folder = g_file_resolve_relative_path (base, text);
              g_clear_object (&base);
              if (!g_file_is_native (folder) || g_file_query_file_type (folder, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY)
                {
                  g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                               _("Choose an existing local folder for the Directory rule: %s"), text);
                  g_object_unref (folder);
                  g_list_free (rows);
                  thunar_search_options_free (options);
                  return NULL;
                }
              gchar *path = g_file_get_path (folder);
              thunar_search_options_add_directory (options, path,
                  gtk_combo_box_get_active (GTK_COMBO_BOX (rule->action)) == 1);
              g_free (path);
              g_object_unref (folder);
            }
          else
            thunar_search_options_add_rule (options, text,
                                        gtk_combo_box_get_active (GTK_COMBO_BOX (rule->action)) == 1,
                                        gtk_combo_box_get_active (GTK_COMBO_BOX (rule->target)) == 1);
        }
    }
  g_list_free (rows);
  options->name_mode = gtk_combo_box_get_active (GTK_COMBO_BOX (fields->name_mode));
  options->case_sensitive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fields->match_case));
  options->recursive = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fields->recursive));
  options->show_hidden = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fields->hidden));
  options->exclude_directories = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fields->exclude_directories));
  gtk_spin_button_update (GTK_SPIN_BUTTON (fields->min_size));
  gtk_spin_button_update (GTK_SPIN_BUTTON (fields->max_size));
  options->min_size = (guint64) (gtk_spin_button_get_value (GTK_SPIN_BUTTON (fields->min_size)) * 1024);
  options->max_size = (guint64) (gtk_spin_button_get_value (GTK_SPIN_BUTTON (fields->max_size)) * 1024);
  if (options->max_size == 0)
    options->max_size = G_MAXUINT64;
  if (!thunar_search_options_set_dates (options, gtk_entry_get_text (GTK_ENTRY (fields->after)),
                                       gtk_entry_get_text (GTK_ENTRY (fields->before)), error)
      || !thunar_search_options_validate (options, error))
    {
      thunar_search_options_free (options);
      return NULL;
    }
  return options;
}

GFile *
thunar_advanced_search_dialog_get_directory (GtkWidget *dialog)
{
  SearchFields *fields = g_object_get_data (G_OBJECT (dialog), "search-fields");
  return gtk_file_chooser_get_file (GTK_FILE_CHOOSER (fields->directory));
}
