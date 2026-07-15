#pragma once

#include <gtk/gtk.h>

typedef struct {
  char *sdk_path;
  char *bind_address;
  guint timeout_ms;
} TvtAppOptions;

int tvt_application_run(const TvtAppOptions *options, int argc, char **argv);
