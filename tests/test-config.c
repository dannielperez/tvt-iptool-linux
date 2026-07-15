#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"

static void
round_trips_configuration(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *directory = g_dir_make_tmp("tvt-iptool-config-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree char *path = g_build_filename(directory, "nested", "config.ini", NULL);
  TvtAppOptions written = {
    .sdk_path = g_strdup("/private/tvt/sdk"),
    .bind_address = g_strdup("10.20.30.40"),
    .timeout_ms = 2750,
  };
  g_assert_true(tvt_app_options_save_file(&written, path, &error));
  g_assert_no_error(error);

  TvtAppOptions loaded = {0};
  tvt_app_options_init(&loaded);
  g_assert_true(tvt_app_options_load_file(&loaded, path, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(loaded.sdk_path, ==, written.sdk_path);
  g_assert_cmpstr(loaded.bind_address, ==, written.bind_address);
  g_assert_cmpuint(loaded.timeout_ms, ==, written.timeout_ms);

  tvt_app_options_clear(&written);
  tvt_app_options_clear(&loaded);
  g_assert_cmpint(g_remove(path), ==, 0);
  g_autofree char *nested = g_path_get_dirname(path);
  g_assert_cmpint(g_rmdir(nested), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void
missing_file_keeps_defaults(void)
{
  TvtAppOptions options = {0};
  tvt_app_options_init(&options);
  g_autoptr(GError) error = NULL;
  g_assert_true(tvt_app_options_load_file(&options, "/path/that/does/not/exist/config.ini", &error));
  g_assert_no_error(error);
  g_assert_cmpstr(options.sdk_path, ==, "/opt/tvt-iptool/sdk");
  g_assert_null(options.bind_address);
  g_assert_cmpuint(options.timeout_ms, ==, 1500);
  tvt_app_options_clear(&options);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/config/round-trip", round_trips_configuration);
  g_test_add_func("/config/missing-defaults", missing_file_keeps_defaults);
  return g_test_run();
}
