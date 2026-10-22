/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <glib/gstdio.h>
#include <thunar/thunar-application.h>
#include <thunar/thunar-window.h>
#include <thunar/thunar-standard-view.h>
#include <thunar/thunar-location-bar.h>
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
  file = g_file_new_for_path (argv[1]);
  source = thunar_file_get (file, &error);
  g_assert_no_error (error);
  g_object_unref (file);
  file = g_file_new_for_path (argv[2]);
  destination = thunar_file_get (file, &error);
  g_assert_no_error (error);
  g_object_unref (file);
  test_navigation (app, source, destination, FALSE);
  test_navigation (app, source, destination, TRUE);
  g_object_unref (source);
  g_object_unref (destination);
  g_object_unref (app);
  return 0;
}
