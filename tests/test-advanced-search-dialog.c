/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <thunar/thunar-advanced-search-dialog.h>
#include <glib/gstdio.h>

static void
find_named (GtkWidget *widget, const gchar *name, GPtrArray *matches)
{
  if (g_strcmp0 (gtk_widget_get_name (widget), name) == 0)
    g_ptr_array_add (matches, widget);
  if (GTK_IS_CONTAINER (widget))
    {
      GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
      for (GList *lp = children; lp != NULL; lp = lp->next)
        find_named (lp->data, name, matches);
      g_list_free (children);
    }
}

static GtkWidget *
last_named (GtkWidget *widget, const gchar *name)
{
  GPtrArray *matches = g_ptr_array_new ();
  find_named (widget, name, matches);
  g_assert_cmpuint (matches->len, >, 0);
  GtkWidget *last = g_ptr_array_index (matches, matches->len - 1);
  g_ptr_array_unref (matches);
  return last;
}

static gboolean
finish_preview (gpointer user_data)
{
  GtkWidget *dialog = user_data;
  GtkAllocation allocation;
  cairo_surface_t *surface;
  cairo_t *cr;
  gtk_widget_get_allocation (dialog, &allocation);
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
  cr = cairo_create (surface);
  gtk_widget_draw (dialog, cr);
  g_assert_cmpint (cairo_surface_write_to_png (surface, g_getenv ("THUNAR_SEARCH_DIALOG_SCREENSHOT")), ==, CAIRO_STATUS_SUCCESS);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  gtk_main_quit ();
  return G_SOURCE_REMOVE;
}

static void
dispatch_for (gint64 microseconds)
{
  gint64 deadline = g_get_monotonic_time () + microseconds;
  do
    {
      while (g_main_context_iteration (NULL, FALSE));
      g_usleep (1000);
    }
  while (g_get_monotonic_time () < deadline);
}

static gboolean
completion_popup_visible (GtkWidget *widget, GtkTreeModel *model)
{
  if (GTK_IS_TREE_VIEW (widget) && gtk_widget_get_mapped (widget))
    {
      GtkTreeModel *shown = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
      if (GTK_IS_TREE_MODEL_FILTER (shown) && gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (shown)) == model)
        return TRUE;
    }
  if (GTK_IS_CONTAINER (widget))
    {
      GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
      gboolean found = FALSE;
      for (GList *lp = children; lp != NULL && !found; lp = lp->next)
        found = completion_popup_visible (lp->data, model);
      g_list_free (children);
      return found;
    }
  return FALSE;
}

static void
test_directory_rules (void)
{
  gchar *root = g_dir_make_tmp ("thunar-directory-rules-XXXXXX", NULL);
  gchar *first = g_build_filename (root, "needle-first", NULL);
  gchar *second = g_build_filename (root, "needle-second", NULL);
  gchar *prefix = g_build_filename (root, "needle-", NULL);
  g_assert_cmpint (g_mkdir_with_parents (first, 0700), ==, 0);
  g_assert_cmpint (g_mkdir_with_parents (second, 0700), ==, 0);
  GFile *directory = g_file_new_for_path (root);
  ThunarSearchOptions *options = thunar_search_options_new ();
  GtkWidget *dialog = thunar_advanced_search_dialog_new (NULL, directory, options);
  GtkWidget *target = last_named (dialog, "search-rule-target");
  GtkWidget *entry = last_named (dialog, "search-rule-text");
  gtk_combo_box_set_active (GTK_COMBO_BOX (target), 2);
  /* Automated checks must not capture keyboard input from the user's desktop. */
  gtk_window_set_accept_focus (GTK_WINDOW (dialog), g_getenv ("THUNAR_TEST_REQUIRE_POPUP") != NULL);
  gtk_widget_show (dialog);
  gtk_widget_grab_focus (entry);
  if (g_getenv ("THUNAR_TEST_REQUIRE_POPUP") != NULL)
    {
      gtk_window_present (GTK_WINDOW (dialog));
      dispatch_for (50000);
      GdkEvent *focus = gdk_event_new (GDK_FOCUS_CHANGE);
      focus->focus_change.in = TRUE;
      focus->focus_change.window = g_object_ref (gtk_widget_get_window (dialog));
      gtk_widget_send_focus_change (dialog, focus);
      gdk_event_free (focus);
      gchar *partial = g_strndup (prefix, strlen (prefix) - 1);
      gtk_entry_set_text (GTK_ENTRY (entry), partial);
      g_free (partial);
      gtk_editable_set_position (GTK_EDITABLE (entry), -1);
      /* Popup grabs need the input device captured from an actual key event. */
      GdkEvent *key = gdk_event_new (GDK_KEY_PRESS);
      key->key.window = g_object_ref (gtk_widget_get_window (entry));
      key->key.send_event = TRUE;
      key->key.keyval = GDK_KEY_minus;
      key->key.string = g_strdup ("-");
      key->key.length = 1;
      gdk_event_set_device (key, gdk_seat_get_keyboard (gdk_display_get_default_seat (gdk_display_get_default ())));
      gtk_main_do_event (key);
      gdk_event_free (key);
    }
  else
    gtk_entry_set_text (GTK_ENTRY (entry), prefix);
  g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (entry)), ==, prefix);
  GtkEntryCompletion *completion = gtk_entry_get_completion (GTK_ENTRY (entry));
  g_assert_nonnull (completion);
  GtkTreeModel *model = gtk_entry_completion_get_model (completion);
  gint64 deadline = g_get_monotonic_time () + 4 * G_USEC_PER_SEC;
  while (gtk_tree_model_iter_n_children (model, NULL) < 2 && g_get_monotonic_time () < deadline)
    dispatch_for (10000);
  g_assert_cmpint (gtk_tree_model_iter_n_children (model, NULL), ==, 2);
  dispatch_for (500000);
  GList *windows = gtk_window_list_toplevels ();
  gboolean visible = FALSE;
  for (GList *lp = windows; lp != NULL; lp = lp->next)
    {
      visible |= completion_popup_visible (lp->data, model);
    }
  g_list_free (windows);
  /* Mapping/grabbing a popup depends on desktop focus policy. Keep the model
   * and selection checks mandatory, with an opt-in check for interactive QA. */
  if (g_getenv ("THUNAR_TEST_REQUIRE_POPUP") != NULL)
    g_assert_true (visible);
  /* Selecting a suggestion copies the actual absolute folder path. */
  GtkTreeIter iter;
  g_assert_true (gtk_tree_model_get_iter_first (model, &iter));
  gboolean handled = FALSE;
  g_signal_emit_by_name (completion, "match-selected", model, &iter, &handled);
  GError *error = NULL;
  ThunarSearchOptions *result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (result->rules->len, ==, 1);
  ThunarSearchRule *rule = g_ptr_array_index (result->rules, 0);
  g_assert_true (rule->is_directory);
  g_assert_false (rule->exclude);
  g_assert_true (g_path_is_absolute (rule->text));
  gtk_button_clicked (GTK_BUTTON (last_named (dialog, "add-search-rule")));
  gtk_combo_box_set_active (GTK_COMBO_BOX (last_named (dialog, "search-rule-target")), 2);
  gtk_combo_box_set_active (GTK_COMBO_BOX (last_named (dialog, "search-rule-action")), 1);
  gtk_entry_set_text (GTK_ENTRY (last_named (dialog, "search-rule-text")), second);
  thunar_search_options_free (result);
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (result->rules->len, ==, 2);
  rule = g_ptr_array_index (result->rules, 1);
  g_assert_true (rule->is_directory);
  g_assert_true (rule->exclude);
  GtkWidget *reopened = thunar_advanced_search_dialog_new (NULL, directory, result);
  ThunarSearchOptions *saved = thunar_advanced_search_dialog_get_options (reopened, &error);
  g_assert_no_error (error);
  g_assert_true (((ThunarSearchRule *) g_ptr_array_index (saved->rules, 1))->is_directory);
  g_assert_true (((ThunarSearchRule *) g_ptr_array_index (saved->rules, 1))->exclude);
  thunar_search_options_free (saved);
  thunar_search_options_free (result);
  gtk_widget_destroy (reopened);
  gtk_entry_set_text (GTK_ENTRY (last_named (dialog, "search-rule-text")), "/does-not-exist-thunar-directory");
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_null (result);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  /* Destruction cancels both queued and running suggestion requests. */
  gtk_widget_destroy (dialog);
  dispatch_for (300000);
  thunar_search_options_free (options);
  g_object_unref (directory);
  g_rmdir (first);
  g_rmdir (second);
  g_rmdir (root);
  g_free (prefix);
  g_free (second);
  g_free (first);
  g_free (root);
  g_print ("Directory rule suggestions, selection, validation and round trip passed\n");
}

int
main (int argc, char **argv)
{
  ThunarSearchOptions *options;
  ThunarSearchOptions *result;
  GtkWidget *dialog;
  GtkWidget *grid = NULL;
  GList *children;
  GFile *directory;
  GFile *selected;
  GError *error = NULL;

  g_test_init (&argc, &argv, NULL);
  if (!gtk_init_check (&argc, &argv))
    {
      g_print ("No display available; skipping GTK dialog test\n");
      return 77;
    }
  test_directory_rules ();
  directory = g_file_new_for_path (g_get_tmp_dir ());
  options = thunar_search_options_new ();
  g_free (options->name);
  options->name = g_strdup ("report*.txt");
  options->name_mode = THUNAR_SEARCH_NAME_WILDCARD;
  g_free (options->content);
  options->content = g_strdup ("quarterly budget");
  thunar_search_options_add_rule (options, "draft", TRUE, FALSE);
  thunar_search_options_add_rule (options, "approved", FALSE, TRUE);
  options->min_size = 1024;
  options->max_size = 102400;
  options->case_sensitive = TRUE;
  options->recursive = FALSE;
  options->show_hidden = TRUE;
  g_assert_true (thunar_search_options_set_dates (options, "2024-02-29", "2024-03-01", &error));
  g_assert_no_error (error);
  dialog = thunar_advanced_search_dialog_new (NULL, directory, options);
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_nonnull (result);
  g_assert_cmpstr (result->name, ==, options->name);
  g_assert_cmpstr (result->content, ==, "");
  g_assert_cmpuint (result->rules->len, ==, 3);
  ThunarSearchRule *rule = g_ptr_array_index (result->rules, 0);
  g_assert_cmpstr (rule->text, ==, "quarterly budget");
  g_assert_true (rule->in_contents);
  g_assert_false (rule->exclude);
  rule = g_ptr_array_index (result->rules, 1);
  g_assert_cmpstr (rule->text, ==, "draft");
  g_assert_false (rule->in_contents);
  g_assert_true (rule->exclude);
  g_assert_cmpint (result->name_mode, ==, options->name_mode);
  g_assert_cmpuint (result->min_size, ==, options->min_size);
  g_assert_cmpuint (result->max_size, ==, options->max_size);
  g_assert_cmpint (result->modified_after, ==, options->modified_after);
  g_assert_cmpint (result->modified_before, ==, options->modified_before);
  g_assert_true (result->case_sensitive);
  g_assert_false (result->recursive);
  g_assert_true (result->show_hidden);
  g_assert_false (result->exclude_directories);
  thunar_search_options_free (result);

  GtkWidget *exclude = last_named (dialog, "exclude-directories");
  g_assert_false (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (exclude)));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (exclude), TRUE);
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_true (result->exclude_directories);
  ThunarSearchOptions *copied = thunar_search_options_copy (result);
  g_assert_true (copied->exclude_directories);
  thunar_search_options_free (copied);
  thunar_search_options_free (result);

  gtk_button_clicked (GTK_BUTTON (last_named (dialog, "add-search-rule")));
  g_assert_cmpint (gtk_combo_box_get_active (GTK_COMBO_BOX (last_named (dialog, "search-rule-target"))), ==, 0);
  g_assert_cmpint (gtk_combo_box_get_active (GTK_COMBO_BOX (last_named (dialog, "search-rule-action"))), ==, 0);
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (result->rules->len, ==, 3); /* Blank rows are ignored. */
  thunar_search_options_free (result);
  gtk_entry_set_text (GTK_ENTRY (last_named (dialog, "search-rule-text")), "archived");
  gtk_combo_box_set_active (GTK_COMBO_BOX (last_named (dialog, "search-rule-action")), 1);
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (result->rules->len, ==, 4);
  rule = g_ptr_array_index (result->rules, 3);
  g_assert_cmpstr (rule->text, ==, "archived");
  g_assert_true (rule->exclude);
  g_assert_false (rule->in_contents);
  GtkWidget *reopened = thunar_advanced_search_dialog_new (NULL, directory, result);
  ThunarSearchOptions *roundtrip = thunar_advanced_search_dialog_get_options (reopened, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (roundtrip->rules->len, ==, result->rules->len);
  g_assert_true (roundtrip->exclude_directories);
  for (guint i = 0; i < result->rules->len; ++i)
    {
      ThunarSearchRule *a = g_ptr_array_index (result->rules, i);
      ThunarSearchRule *b = g_ptr_array_index (roundtrip->rules, i);
      g_assert_cmpstr (a->text, ==, b->text);
      g_assert_cmpint (a->exclude, ==, b->exclude);
      g_assert_cmpint (a->in_contents, ==, b->in_contents);
    }
  thunar_search_options_free (roundtrip);
  thunar_search_options_free (result);
  gtk_widget_destroy (reopened);
  gtk_button_clicked (GTK_BUTTON (last_named (dialog, "remove-search-rule")));
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (result->rules->len, ==, 3);
  thunar_search_options_free (result);

  if (g_getenv ("THUNAR_SEARCH_DIALOG_SCREENSHOT") != NULL)
    {
      ThunarSearchOptions *preview_options = thunar_search_options_copy (options);
      g_ptr_array_set_size (preview_options->rules, 0);
      g_free (preview_options->content);
      preview_options->content = g_strdup ("");
      thunar_search_options_add_rule (preview_options, "report", FALSE, FALSE);
      thunar_search_options_add_directory (preview_options, g_get_home_dir (), FALSE);
      if (g_file_test (g_get_user_cache_dir (), G_FILE_TEST_IS_DIR))
        thunar_search_options_add_directory (preview_options, g_get_user_cache_dir (), TRUE);
      GtkWidget *preview = thunar_advanced_search_dialog_new (NULL, directory, preview_options);
      thunar_search_options_free (preview_options);
      gtk_widget_show (preview);
      g_timeout_add (150, finish_preview, preview);
      gtk_main ();
      gtk_widget_destroy (preview);
    }
  while (gtk_events_pending ())
    gtk_main_iteration ();
  selected = thunar_advanced_search_dialog_get_directory (dialog);
  g_assert_nonnull (selected);
  g_assert_true (g_file_equal (selected, directory));
  g_object_unref (selected);

  children = gtk_container_get_children (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (dialog))));
  for (GList *lp = children; lp != NULL; lp = lp->next)
    if (GTK_IS_GRID (lp->data))
      grid = lp->data;
  g_list_free (children);
  g_assert_nonnull (grid);
  gtk_entry_set_text (GTK_ENTRY (gtk_grid_get_child_at (GTK_GRID (grid), 1, 1)), "(");
  gtk_combo_box_set_active (GTK_COMBO_BOX (gtk_grid_get_child_at (GTK_GRID (grid), 1, 2)), THUNAR_SEARCH_NAME_REGEX);
  result = thunar_advanced_search_dialog_get_options (dialog, &error);
  g_assert_null (result);
  g_assert_nonnull (error);
  g_clear_error (&error);
  gtk_widget_destroy (dialog);
  thunar_search_options_free (options);
  g_object_unref (directory);
  g_print ("Advanced Search dialog: add/remove rules, filename default, option round trip, directory and validation passed\n");
  return 0;
}
