/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <thunar/thunar-advanced-search-dialog.h>

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
  directory = g_file_new_for_path (g_get_tmp_dir ());
  options = thunar_search_options_new ();
  g_free (options->name);
  options->name = g_strdup ("report*.txt");
  options->name_mode = THUNAR_SEARCH_NAME_WILDCARD;
  g_free (options->content);
  options->content = g_strdup ("quarterly budget");
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
  g_assert_cmpstr (result->content, ==, options->content);
  g_assert_cmpint (result->name_mode, ==, options->name_mode);
  g_assert_cmpuint (result->min_size, ==, options->min_size);
  g_assert_cmpuint (result->max_size, ==, options->max_size);
  g_assert_cmpint (result->modified_after, ==, options->modified_after);
  g_assert_cmpint (result->modified_before, ==, options->modified_before);
  g_assert_true (result->case_sensitive);
  g_assert_false (result->recursive);
  g_assert_true (result->show_hidden);
  thunar_search_options_free (result);

  if (g_getenv ("THUNAR_SEARCH_DIALOG_SCREENSHOT") != NULL)
    {
      gtk_widget_show (dialog);
      g_timeout_add (150, finish_preview, dialog);
      gtk_main ();
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
  g_print ("Advanced Search dialog: option round trip, directory and validation passed\n");
  return 0;
}
