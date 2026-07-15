#include "application.h"
#include "discovery.h"
#include "sdk-loader.h"

#ifndef TVT_GUI_VARIANT
#define TVT_GUI_VARIANT "unknown"
#endif

int
main(int argc, char **argv)
{
  TvtAppOptions options = { .timeout_ms = 1500 };
  gboolean show_version = FALSE;
  gboolean check_sdk = FALSE;
  gboolean discover_only = FALSE;
  GOptionEntry entries[] = {
    { "sdk-path", 0, 0, G_OPTION_ARG_FILENAME, &options.sdk_path,
      "Path to a vendor-supplied libdvrnetsdk.so or its directory", "PATH" },
    { "bind-address", 0, 0, G_OPTION_ARG_STRING, &options.bind_address,
      "IPv4 address of the LAN interface used for multicast discovery", "IP" },
    { "timeout-ms", 0, 0, G_OPTION_ARG_INT, &options.timeout_ms,
      "Discovery response timeout per attempt", "MILLISECONDS" },
    { "check-sdk", 0, 0, G_OPTION_ARG_NONE, &check_sdk,
      "Check the private SDK path and exit without opening the GUI", NULL },
    { "discover-only", 0, 0, G_OPTION_ARG_NONE, &discover_only,
      "Run LAN discovery, print a tab-separated inventory, and exit", NULL },
    { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version, "Print version and exit", NULL },
    { NULL }
  };

  g_autoptr(GOptionContext) context = g_option_context_new("— discover and configure TVT cameras on Linux");
  g_option_context_add_main_entries(context, entries, NULL);
  g_option_context_set_ignore_unknown_options(context, TRUE);
  g_autoptr(GError) error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("tvt-iptool: %s\n", error->message);
    return 2;
  }
  if (show_version) {
    g_print("tvt-iptool 0.1.2 (%s)\n", TVT_GUI_VARIANT);
    return 0;
  }
  if (check_sdk) {
    g_autofree char *detail = NULL;
    if (!tvt_sdk_is_available(options.sdk_path, &detail)) {
      g_printerr("SDK unavailable: %s\n", detail);
      g_free(options.sdk_path);
      g_free(options.bind_address);
      return 1;
    }
    g_print("SDK available: %s\n", detail);
    g_free(options.sdk_path);
    g_free(options.bind_address);
    return 0;
  }
  if (discover_only) {
    TvtDiscoveryOptions discovery_options = {
      .timeout_ms = options.timeout_ms,
      .retries = 2,
      .bind_address = options.bind_address,
    };
    g_autoptr(GError) discover_error = NULL;
    GPtrArray *devices = tvt_discover_lan(&discovery_options, NULL, &discover_error);
    if (!devices) {
      g_printerr("Discovery failed: %s\n", discover_error->message);
      g_free(options.sdk_path);
      g_free(options.bind_address);
      return 1;
    }
    g_print("TYPE\tIP\tMAC\tMODEL\tNAME\tFIRMWARE\tDATA\tHTTP\n");
    for (guint i = 0; i < devices->len; i++) {
      TvtDevice *device = g_ptr_array_index(devices, i);
      g_print("%s\t%s\t%s\t%s\t%s\t%s\t%u\t%u\n",
              tvt_device_get_device_type(device),
              tvt_device_get_ip(device),
              tvt_device_get_mac(device),
              tvt_device_get_model(device),
              tvt_device_get_name(device),
              tvt_device_get_firmware(device),
              tvt_device_get_data_port(device),
              tvt_device_get_http_port(device));
    }
    guint found = devices->len;
    g_ptr_array_unref(devices);
    g_printerr("%u device%s discovered\n", found, found == 1 ? "" : "s");
    g_free(options.sdk_path);
    g_free(options.bind_address);
    return 0;
  }
  if (options.timeout_ms < 250 || options.timeout_ms > 30000) {
    g_printerr("tvt-iptool: --timeout-ms must be between 250 and 30000\n");
    return 2;
  }

  int status = tvt_application_run(&options, argc, argv);
  g_free(options.sdk_path);
  g_free(options.bind_address);
  return status;
}
