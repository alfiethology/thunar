/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <glib/gstdio.h>
#include <thunar/thunar-application.h>
#include <thunar/thunar-window.h>
#include <thunar/thunar-standard-view.h>
#include <thunar/thunar-location-bar.h>
#include <thunar/thunar-list-model.h>
#include <thunar/thunar-column-model.h>
#include <thunar/thunar-gobject-extensions.h>
#include <thunar/thunar-resources.h>
#include <thunar/thunar-util.h>
#include <xfconf/xfconf.h>

static GtkWidget *
find_widget (GtkWidget *widget, GType type)
{
  if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
    return widget;
  if (GTK_IS_NOTEBOOK (widget))
    {
      GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (widget), gtk_notebook_get_current_page (GTK_NOTEBOOK (widget)));
      return page != NULL ? find_widget (page, type) : NULL;
    }
  if (GTK_IS_CONTAINER (widget))
    {
      GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
      GtkWidget *found = NULL;
      for (GList *lp = children; lp != NULL && found == NULL; lp = lp->next)
        found = find_widget (lp->data, type);
      g_list_free (children);
      return found;
    }
  return NULL;
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

static gchar *
size_for_name (ThunarListModel *model, const gchar *name)
{
  GtkTreeIter iter;
  gboolean valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter);
  while (valid)
    {
      gchar *row_name;
      gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, THUNAR_COLUMN_FILE_NAME, &row_name, -1);
      if (g_strcmp0 (row_name, name) == 0)
        {
          gchar *size;
          g_free (row_name);
          gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, THUNAR_COLUMN_SIZE_IN_BYTES, &size, -1);
          return size;
        }
      g_free (row_name);
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter);
    }
  return NULL;
}

static void
assert_folder_size (ThunarListModel *model, const gchar *name, const gchar *expected)
{
  gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
  gchar *size;
  do
    {
      dispatch_for (10000);
      size = size_for_name (model, name);
      if (g_strcmp0 (size, expected) == 0)
        {
          g_free (size);
          return;
        }
      g_free (size);
    }
  while (g_get_monotonic_time () < deadline);
  size = size_for_name (model, name);
  g_assert_cmpstr (size, ==, expected);
  g_free (size);
}

static void
test_folder_sizes (void)
{
  gchar *root = g_dir_make_tmp ("thunar-folder-size-XXXXXX", NULL);
  gchar *nested = g_build_filename (root, "folder/sub", NULL);
  gchar *data = g_build_filename (nested, ".hidden", NULL);
  gchar *loop = g_build_filename (nested, "loop", NULL);
  gchar *empty = g_build_filename (root, "empty", NULL);
  gchar *regular = g_build_filename (root, "file", NULL);
  gchar *payload = g_strnfill (23700, 'x');
  GFile *location;
  GFile *link;
  ThunarFile *file;
  ThunarFolder *folder;
  ThunarListModel *model;
  gchar *size;

  g_assert_cmpint (g_mkdir_with_parents (nested, 0700), ==, 0);
  g_assert_cmpint (g_mkdir (empty, 0700), ==, 0);
  g_assert_true (g_file_set_contents (data, payload, -1, NULL));
  g_assert_true (g_file_set_contents (regular, "test", -1, NULL));
  link = g_file_new_for_path (loop);
  g_assert_true (g_file_make_symbolic_link (link, "..", NULL, NULL));
  g_object_unref (link);
  location = g_file_new_for_path (root);
  file = thunar_file_get (location, NULL);
  folder = thunar_folder_get_for_file (file);
  model = thunar_list_model_new ();
  thunar_list_model_set_folder (model, folder, NULL);
  /* Hidden descendants count; directory metadata does not; loops are not followed. */
  assert_folder_size (model, "folder", "23.7KB");
  assert_folder_size (model, "empty", "0.0B");
  assert_folder_size (model, "file", "4.0B");
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), THUNAR_COLUMN_SIZE_IN_BYTES, GTK_SORT_DESCENDING);
  GtkTreeIter first;
  gchar *first_name;
  g_assert_true (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &first));
  gtk_tree_model_get (GTK_TREE_MODEL (model), &first, THUNAR_COLUMN_FILE_NAME, &first_name, -1);
  g_assert_cmpstr (first_name, ==, "folder");
  g_free (first_name);

  g_assert_true (g_file_set_contents (data, "a", -1, NULL));
  thunar_folder_reload (folder, TRUE);
  /* Refresh invalidates the cache, including changes deep inside a subfolder. */
  assert_folder_size (model, "folder", "3.0B");

  /* Start a fresh scan and navigate/dispose before its completion is dispatched. */
  thunar_list_model_set_folder (model, folder, NULL);
  size = size_for_name (model, "folder");
  g_assert_cmpstr (size, ==, "Calculating...");
  g_free (size);
  thunar_list_model_set_folder (model, NULL, NULL);
  g_object_unref (model);
  dispatch_for (150000);
  g_object_unref (folder);
  g_object_unref (file);
  g_object_unref (location);
  g_unlink (loop);
  g_unlink (data);
  g_unlink (regular);
  g_rmdir (nested);
  gchar *parent = g_path_get_dirname (nested);
  g_rmdir (parent);
  g_free (parent);
  g_rmdir (empty);
  g_rmdir (root);
  g_free (payload);
  g_free (regular);
  g_free (empty);
  g_free (loop);
  g_free (data);
  g_free (nested);
  g_free (root);
  g_print ("Folder totals, refresh and pending-scan navigation passed\n");
}

static void
test_column_visibility (ThunarApplication *app, ThunarFile *source, gboolean fixed)
{
  ThunarPreferences *preferences = thunar_preferences_get ();
  ThunarColumnModel *columns = thunar_column_model_get_default ();
  g_object_set (preferences, "last-view", "ThunarDetailsView", "default-view", "ThunarDetailsView",
                "last-details-view-fixed-columns", fixed,
                "last-details-view-visible-columns", "THUNAR_COLUMN_NAME,THUNAR_COLUMN_TYPE", NULL);
  GtkWidget *window = thunar_application_open_window (app, source, NULL, NULL, TRUE);
  gtk_window_resize (GTK_WINDOW (window), 900, 600);
  dispatch_for (200000);
  GtkWidget *view = find_widget (window, THUNAR_TYPE_STANDARD_VIEW);
  GtkTreeView *tree = GTK_TREE_VIEW (gtk_bin_get_child (GTK_BIN (view)));
  GList *tree_columns = gtk_tree_view_get_columns (tree);
  /* A wide existing column puts newly enabled columns beyond the viewport. */
  for (GList *lp = tree_columns; lp != NULL; lp = lp->next)
    if (gtk_tree_view_column_get_sort_column_id (lp->data) == THUNAR_COLUMN_TYPE)
      gtk_tree_view_column_set_min_width (lp->data, 1000);
  dispatch_for (100000);
  ThunarColumn toggles[] = { THUNAR_COLUMN_DATE_CREATED, THUNAR_COLUMN_DATE_MODIFIED, THUNAR_COLUMN_PERMISSIONS };
  for (guint i = 0; i < G_N_ELEMENTS (toggles); ++i)
    {
      thunar_column_model_set_column_visible (columns, toggles[i], TRUE);
      /* GTK animates horizontal scrolling; allow the transition to finish. */
      dispatch_for (350000);
      for (GList *lp = tree_columns; lp != NULL; lp = lp->next)
        if (gtk_tree_view_column_get_sort_column_id (lp->data) == toggles[i])
          {
            GdkRectangle cell;
            GtkTreePath *path = gtk_tree_path_new_first ();
            g_assert_true (gtk_tree_view_column_get_visible (lp->data));
            gtk_tree_view_get_cell_area (tree, path, lp->data, &cell);
            gtk_tree_view_convert_bin_window_to_widget_coords (tree, cell.x, cell.y, &cell.x, &cell.y);
            g_print ("Enabled column %d: x=%d, width=%d, viewport=%d\n", toggles[i], cell.x, cell.width, gtk_widget_get_allocated_width (GTK_WIDGET (tree)));
            g_assert_cmpint (cell.x, >=, 0);
            g_assert_cmpint (cell.x + cell.width, <=, gtk_widget_get_allocated_width (GTK_WIDGET (tree)));
            gtk_tree_path_free (path);
          }
    }
  thunar_list_model_set_folder (THUNAR_STANDARD_VIEW (view)->model, NULL, NULL);
  thunar_column_model_set_column_visible (columns, THUNAR_COLUMN_DATE_ACCESSED, TRUE);
  dispatch_for (350000);
  for (GList *lp = tree_columns; lp != NULL; lp = lp->next)
    if (gtk_tree_view_column_get_sort_column_id (lp->data) == THUNAR_COLUMN_DATE_ACCESSED)
      {
        GdkRectangle area;
        gtk_tree_view_get_cell_area (tree, NULL, lp->data, &area);
        gtk_tree_view_convert_bin_window_to_widget_coords (tree, area.x, area.y, &area.x, &area.y);
        g_assert_cmpint (area.x, >=, 0);
        g_assert_cmpint (area.x + area.width, <=, gtk_widget_get_allocated_width (GTK_WIDGET (tree)));
      }
  g_list_free (tree_columns);
  gtk_widget_destroy (window);
  dispatch_for (50000);
  g_object_unref (columns);
  g_object_unref (preferences);
  g_print ("Newly enabled columns are visible in the viewport\n");
}

static void
test_navigation (ThunarApplication *app, ThunarFile *source, ThunarFile *destination, gboolean pending)
{
  ThunarPreferences *preferences = thunar_preferences_get ();
  GtkWidget *window;
  GtkWidget *location;
  ThunarStandardView *view;
  gchar *query;
  gint64 deadline;

  g_object_set (preferences, "last-view", "ThunarDetailsView", "default-view", "ThunarDetailsView", NULL);
  if (pending)
    g_assert_true (g_file_set_contents (g_getenv ("THUNAR_NAVIGATION_SLOW_FILE"), "", 0, NULL));
  else
    g_unlink (g_getenv ("THUNAR_NAVIGATION_SLOW_FILE"));

  window = thunar_application_open_window (app, source, NULL, NULL, TRUE);
  dispatch_for (150000);
  thunar_window_action_search (THUNAR_WINDOW (window));
  location = find_widget (window, THUNAR_TYPE_LOCATION_BAR);
  g_assert_nonnull (location);
  query = g_strconcat (SEARCH_PREFIX, "the", NULL);
  thunar_location_bar_request_entry (THUNAR_LOCATION_BAR (location), query, FALSE);
  thunar_window_update_search (THUNAR_WINDOW (window));
  g_free (query);
  view = THUNAR_STANDARD_VIEW (find_widget (window, THUNAR_TYPE_STANDARD_VIEW));
  deadline = g_get_monotonic_time () + 2 * G_USEC_PER_SEC;
  while (g_get_monotonic_time () < deadline)
    {
      if (pending ? gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->model), NULL) > 0
                  : thunar_list_model_get_job (view->model) == NULL)
        break;
      dispatch_for (10000);
    }
  g_assert_cmpint (gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->model), NULL), ==, 1);
  if (pending)
    g_assert_nonnull (thunar_list_model_get_job (view->model));
  else
    g_assert_null (thunar_list_model_get_job (view->model));

  /* Navigation can select a different view type for the destination. Before
   * the fix this handed the same job to two models and crashed on completion. */
  g_object_set (preferences, "last-view", "ThunarIconView", NULL);
  GtkWidget *sidebar = thunar_window_get_sidepane (THUNAR_WINDOW (window));
  g_assert_nonnull (sidebar);
  g_signal_emit_by_name (sidebar, "change-directory", destination);
  dispatch_for (150000);
  view = THUNAR_STANDARD_VIEW (find_widget (window, THUNAR_TYPE_STANDARD_VIEW));
  g_assert_nonnull (view);
  g_assert_null (thunar_standard_view_get_search_query (view));
  g_assert_null (thunar_list_model_get_job (view->model));
  g_assert_null (thunar_list_model_get_advanced_search (view->model));
  g_assert_true (thunar_window_get_current_directory (THUNAR_WINDOW (window)) == destination);
  gtk_widget_destroy (window);
  dispatch_for (50000);
  g_object_unref (preferences);
  g_print ("Navigation after %s search passed\n", pending ? "pending" : "completed");
}

int main (int argc, char **argv)
{
  ThunarApplication *app;
  ThunarFile *source;
  ThunarFile *destination;
  GFile *file;
  GError *error = NULL;

  if (!gtk_init_check (&argc, &argv))
    return 77;
  g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  g_assert_nonnull (thunar_get_resource ());
  g_assert_true (xfconf_init (&error));
  g_assert_no_error (error);
  thunar_g_initialize_transformations ();
  app = thunar_application_get ();
  g_assert_true (g_application_register (G_APPLICATION (app), NULL, &error));
  g_assert_no_error (error);
  test_folder_sizes ();
  file = g_file_new_for_path (argv[1]);
  source = thunar_file_get (file, &error);
  g_assert_no_error (error);
  g_object_unref (file);
  file = g_file_new_for_path (argv[2]);
  destination = thunar_file_get (file, &error);
  g_assert_no_error (error);
  g_object_unref (file);
  test_column_visibility (app, source, FALSE);
  test_column_visibility (app, source, TRUE);
  test_navigation (app, source, destination, FALSE);
  test_navigation (app, source, destination, TRUE);
  g_object_unref (source);
  g_object_unref (destination);
  g_object_unref (app);
  return 0;
}
