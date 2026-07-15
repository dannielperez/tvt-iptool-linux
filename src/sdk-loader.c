#include "sdk-loader.h"

#include <dlfcn.h>
#ifndef TVT_DEFAULT_SDK_DIR
#define TVT_DEFAULT_SDK_DIR "/opt/tvt-iptool/sdk"
#endif

typedef int (*SdkInitFunc)(void);
typedef int (*SdkCleanupFunc)(void);
typedef unsigned int (*SdkGetLastErrorFunc)(void);
typedef int (*SdkSetDeviceIpFunc)(const char *, const char *, const char *, const char *,
                                  const char *, const char *, const char *);
typedef int (*SdkModifyNetInfoFunc)(void *);

typedef struct {
  void *handle;
  char *loaded_path;
  SdkInitFunc init;
  SdkCleanupFunc cleanup;
  SdkGetLastErrorFunc get_last_error;
  SdkSetDeviceIpFunc set_device_ip;
  SdkModifyNetInfoFunc modify_net_info;
} SdkHandle;

G_DEFINE_QUARK(tvt-sdk-error-quark, tvt_sdk_error)

static char *
resolve_path(const char *sdk_path)
{
  const char *configured = sdk_path && *sdk_path ? sdk_path : g_getenv("TVT_SDK_PATH");
  if (!configured || !*configured) {
    g_autofree char *default_path =
      g_build_filename(TVT_DEFAULT_SDK_DIR, "libdvrnetsdk.so", NULL);
    if (g_file_test(default_path, G_FILE_TEST_IS_REGULAR))
      return g_steal_pointer(&default_path);
    return g_strdup("libdvrnetsdk.so");
  }
  if (g_file_test(configured, G_FILE_TEST_IS_DIR))
    return g_build_filename(configured, "libdvrnetsdk.so", NULL);
  return g_strdup(configured);
}

static void
sdk_close(SdkHandle *sdk)
{
  if (!sdk)
    return;
  if (sdk->handle)
    dlclose(sdk->handle);
  g_free(sdk->loaded_path);
  g_free(sdk);
}

static SdkHandle *
sdk_open(const char *sdk_path, GError **error)
{
  g_autofree char *path = resolve_path(sdk_path);
  dlerror();
  void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char *detail = dlerror();
    g_set_error(error, TVT_SDK_ERROR, TVT_SDK_ERROR_NOT_FOUND,
                "Could not load %s: %s. Install the vendor SDK in %s or set TVT_SDK_PATH.",
                path, detail ? detail : "unknown loader error", TVT_DEFAULT_SDK_DIR);
    return NULL;
  }

  SdkHandle *sdk = g_new0(SdkHandle, 1);
  sdk->handle = handle;
  sdk->loaded_path = g_strdup(path);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
  sdk->init = (SdkInitFunc)dlsym(handle, "NET_SDK_Init");
  sdk->cleanup = (SdkCleanupFunc)dlsym(handle, "NET_SDK_Cleanup");
  sdk->get_last_error = (SdkGetLastErrorFunc)dlsym(handle, "NET_SDK_GetLastError");
  sdk->set_device_ip = (SdkSetDeviceIpFunc)dlsym(handle, "NET_SDK_SetDeviceIP");
  sdk->modify_net_info = (SdkModifyNetInfoFunc)dlsym(handle, "NET_SDK_ModifyDeviceNetInfo");
#pragma GCC diagnostic pop

  if (!sdk->init || !sdk->cleanup || (!sdk->set_device_ip && !sdk->modify_net_info)) {
    g_set_error(error, TVT_SDK_ERROR, TVT_SDK_ERROR_INCOMPATIBLE,
                "%s is not a compatible TVT device SDK: required initialization and IP-change symbols are missing.",
                path);
    sdk_close(sdk);
    return NULL;
  }
  return sdk;
}

gboolean
tvt_sdk_is_available(const char *sdk_path, char **detail)
{
  g_autoptr(GError) error = NULL;
  SdkHandle *sdk = sdk_open(sdk_path, &error);
  if (!sdk) {
    if (detail)
      *detail = g_strdup(error->message);
    return FALSE;
  }
  if (!sdk->init()) {
    guint code = sdk->get_last_error ? sdk->get_last_error() : 0;
    if (detail)
      *detail = g_strdup_printf("Loaded %s, but SDK initialization failed (error %u)",
                                sdk->loaded_path, code);
    sdk_close(sdk);
    return FALSE;
  }
  sdk->cleanup();
  if (detail)
    *detail = g_strdup_printf("Loaded and initialized %s (%s)", sdk->loaded_path,
                              sdk->set_device_ip ? "NET_SDK_SetDeviceIP" : "NET_SDK_ModifyDeviceNetInfo");
  sdk_close(sdk);
  return TRUE;
}
