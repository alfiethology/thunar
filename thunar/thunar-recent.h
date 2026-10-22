/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __THUNAR_RECENT_H__
#define __THUNAR_RECENT_H__

#include <gio/gio.h>

#define THUNAR_RECENT_FILES_LIMIT 200

/* Returns owned GFileInfo references, newest first. */
GList *thunar_recent_collect_infos (GFileEnumerator *enumerator,
                                    guint            limit,
                                    GCancellable    *cancellable,
                                    GError         **error);

#endif
