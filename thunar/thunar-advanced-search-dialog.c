/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <glib/gi18n.h>
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

static GtkWidget *
add_rule (SearchFields *fields, const gchar *text, gboolean exclude, gboolean in_contents)
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
  gtk_combo_box_set_active (GTK_COMBO_BOX (rule->target), in_contents ? 1 : 0);
  rule->text = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (rule->text), text);
  gtk_entry_set_placeholder_text (GTK_ENTRY (rule->text), _("Text to match (literal)"));
  gtk_entry_set_activates_default (GTK_ENTRY (rule->text), TRUE);
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
  add_field (grid, _("Text _rules:"), rule_box, 3);
  if (*options->content != '\0')
    add_rule (fields, options->content, FALSE, TRUE);
  for (guint i = 0; i < options->rules->len; ++i)
    {
      ThunarSearchRule *rule = g_ptr_array_index (options->rules, i);
      add_rule (fields, rule->text, rule->exclude, rule->in_contents);
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

  help = gtk_label_new (_("All include rules must match. Excluded text must not occur.\n"
                         "Rules match literal text; empty rules are ignored. Filename is the default.\n"
                         "Content rules return files only. Maximum size 0 means unlimited.\n"
                         "Both dates include the whole day."));
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
        thunar_search_options_add_rule (options, text,
                                        gtk_combo_box_get_active (GTK_COMBO_BOX (rule->action)) == 1,
                                        gtk_combo_box_get_active (GTK_COMBO_BOX (rule->target)) == 1);
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
