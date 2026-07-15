#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#define SETTINGS_GROUP "application"
#define DEFAULT_SDK_PATH "/opt/tvt-iptool/sdk"
#define DEFAULT_TIMEOUT_MS 1500

static char *
settings_path(void)
{
  return g_build_filename(g_get_user_config_dir(), "tvt-iptool", "config.ini", NULL);
}

void
tvt_app_options_clear(TvtAppOptions *options)
{
  if (!options)
    return;
  g_clear_pointer(&options->sdk_path, g_free);
  g_clear_pointer(&options->bind_address, g_free);
  options->timeout_ms = 0;
}

void
tvt_app_options_init(TvtAppOptions *options)
{
  g_return_if_fail(options != NULL);
  *options = (TvtAppOptions) {
    .sdk_path = g_strdup(DEFAULT_SDK_PATH),
    .timeout_ms = DEFAULT_TIMEOUT_MS,
  };
}

gboolean
tvt_app_options_load_file(TvtAppOptions *options, const char *path, GError **error)
{
  g_return_val_if_fail(options != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_autoptr(GKeyFile) file = g_key_file_new();
  g_autoptr(GError) load_error = NULL;
  if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, &load_error)) {
    if (g_error_matches(load_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
      return TRUE;
    g_propagate_error(error, g_steal_pointer(&load_error));
    return FALSE;
  }

  g_autofree char *sdk_path = g_key_file_get_string(file, SETTINGS_GROUP, "sdk-path", NULL);
  g_autofree char *bind_address = g_key_file_get_string(file, SETTINGS_GROUP, "bind-address", NULL);
  g_autoptr(GError) timeout_error = NULL;
  gint timeout = g_key_file_get_integer(file, SETTINGS_GROUP, "timeout-ms", &timeout_error);
  if (sdk_path && *sdk_path) {
    g_free(options->sdk_path);
    options->sdk_path = g_steal_pointer(&sdk_path);
  }
  if (bind_address && *bind_address) {
    g_free(options->bind_address);
    options->bind_address = g_steal_pointer(&bind_address);
  } else {
    g_clear_pointer(&options->bind_address, g_free);
  }
  if (!timeout_error && timeout >= 250 && timeout <= 30000)
    options->timeout_ms = (guint)timeout;
  return TRUE;
}

gboolean
tvt_app_options_save_file(const TvtAppOptions *options, const char *path, GError **error)
{
  g_return_val_if_fail(options != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_autoptr(GKeyFile) file = g_key_file_new();
  g_key_file_set_string(file, SETTINGS_GROUP, "sdk-path",
                        options->sdk_path ? options->sdk_path : DEFAULT_SDK_PATH);
  g_key_file_set_string(file, SETTINGS_GROUP, "bind-address",
                        options->bind_address ? options->bind_address : "");
  g_key_file_set_integer(file, SETTINGS_GROUP, "timeout-ms", (gint)options->timeout_ms);
  g_autofree char *data = g_key_file_to_data(file, NULL, NULL);
  g_autofree char *directory = g_path_get_dirname(path);
  if (g_mkdir_with_parents(directory, 0700) != 0) {
    int saved_errno = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                "Could not create configuration directory %s: %s", directory,
                g_strerror(saved_errno));
    return FALSE;
  }
  return g_file_set_contents(path, data, -1, error);
}

gboolean
tvt_app_options_load(TvtAppOptions *options, GError **error)
{
  g_autofree char *path = settings_path();
  return tvt_app_options_load_file(options, path, error);
}

gboolean
tvt_app_options_save(const TvtAppOptions *options, GError **error)
{
  g_autofree char *path = settings_path();
  return tvt_app_options_save_file(options, path, error);
}
