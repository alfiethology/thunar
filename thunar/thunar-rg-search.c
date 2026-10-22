/* vi:set et ai sw=2 sts=2 ts=2: */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "thunar-rg-search.h"



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
  GSubprocessLauncher *launcher;
  GSubprocess         *process;
  GDataInputStream    *stream;
  GError             *err = NULL;
  gchar              *directory_path;
  gchar              *path;
  gulong              cancel_id;
  const gchar        *argv[] = {
    "rg", "--no-config", "--files", "--null", "--no-ignore",
    "--color=never", "--no-messages",
    show_hidden ? "--hidden" : "--glob=!*~", "--", ".", NULL
  };

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  directory_path = g_file_get_path (directory);
  if (directory_path == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "ripgrep requires a local search directory");
      return FALSE;
    }

  /* No shell, file contents, ignore rules, or user ripgrep configuration.
   * Only filenames are streamed; metadata is loaded by the caller for matches. */
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                      | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_subprocess_launcher_set_cwd (launcher, directory_path);
  g_free (directory_path);
  process = g_subprocess_launcher_spawnv (launcher, argv, error);
  g_object_unref (launcher);
  if (process == NULL)
    return FALSE;

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
      if (err == NULL && !g_cancellable_is_cancelled (cancellable))
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
  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  return TRUE;
}
