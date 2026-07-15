#pragma once

#include <glib.h>

typedef struct {
  char *sdk_path;
  char *bind_address;
  guint timeout_ms;
} TvtAppOptions;

void tvt_app_options_init(TvtAppOptions *options);
void tvt_app_options_clear(TvtAppOptions *options);
gboolean tvt_app_options_load(TvtAppOptions *options, GError **error);
gboolean tvt_app_options_save(const TvtAppOptions *options, GError **error);

/* File-specific variants keep persistence independently testable. */
gboolean tvt_app_options_load_file(TvtAppOptions *options, const char *path, GError **error);
gboolean tvt_app_options_save_file(const TvtAppOptions *options, const char *path, GError **error);
