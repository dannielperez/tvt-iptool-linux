#include "sdk-loader.h"

#include <dlfcn.h>
#include <string.h>

/*
 * This is an independently defined ABI record for the SDK's legacy LAN
 * provisioning call. It intentionally contains only the network fields used
 * by this application. No vendor header or binary is part of this project.
 */
typedef struct {
  char mac[36];
  char ip_address[64];
  char subnet_mask[36];
  char gateway[36];
  char password[64];
  char dns1[36];
  char dns2[36];
  unsigned char ip_mode;
} TvtSdkIpInfo;

typedef int (*SdkInitFunc)(void);
typedef int (*SdkCleanupFunc)(void);
typedef unsigned int (*SdkGetLastErrorFunc)(void);
typedef int (*SdkSetDeviceIpFunc)(const char *, const char *, const char *, const char *,
                                  const char *, const char *, const char *);
typedef int (*SdkModifyNetInfoFunc)(TvtSdkIpInfo *);

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
  if (!configured || !*configured)
    return g_strdup("libdvrnetsdk.so");
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
                "Could not load %s: %s. Set TVT_SDK_PATH to your vendor-supplied Linux SDK.",
                path, detail ? detail : "unknown loader error");
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
  if (detail)
    *detail = g_strdup_printf("Loaded %s (%s)", sdk->loaded_path,
                              sdk->set_device_ip ? "NET_SDK_SetDeviceIP" : "NET_SDK_ModifyDeviceNetInfo");
  sdk_close(sdk);
  return TRUE;
}

static gboolean
copy_checked(char *destination, gsize capacity, const char *value, const char *field, GError **error)
{
  gsize length = strlen(value ? value : "");
  if (length >= capacity) {
    g_set_error(error, TVT_SDK_ERROR, TVT_SDK_ERROR_ARGUMENT,
                "%s is too long for the SDK interface.", field);
    return FALSE;
  }
  g_strlcpy(destination, value ? value : "", capacity);
  return TRUE;
}

TvtSdkModifyResult *
tvt_sdk_modify_ip(const char *sdk_path, const char *mac, const char *password,
                  const char *new_ip, const char *subnet_mask, const char *gateway,
                  GError **error)
{
  SdkHandle *sdk = sdk_open(sdk_path, error);
  if (!sdk)
    return NULL;

  if (!sdk->init()) {
    guint code = sdk->get_last_error ? sdk->get_last_error() : 0;
    g_set_error(error, TVT_SDK_ERROR, TVT_SDK_ERROR_INITIALIZE,
                "TVT SDK initialization failed (error %u).", code);
    sdk_close(sdk);
    return NULL;
  }

  int ok = 0;
  const char *function_used = NULL;
  if (sdk->set_device_ip) {
    function_used = "NET_SDK_SetDeviceIP";
    ok = sdk->set_device_ip(mac, password ? password : "", new_ip, subnet_mask, gateway, "", "");
  } else {
    function_used = "NET_SDK_ModifyDeviceNetInfo";
    TvtSdkIpInfo payload = {0};
    if (!copy_checked(payload.mac, sizeof(payload.mac), mac, "MAC address", error) ||
        !copy_checked(payload.ip_address, sizeof(payload.ip_address), new_ip, "IP address", error) ||
        !copy_checked(payload.subnet_mask, sizeof(payload.subnet_mask), subnet_mask, "subnet mask", error) ||
        !copy_checked(payload.gateway, sizeof(payload.gateway), gateway, "gateway", error) ||
        !copy_checked(payload.password, sizeof(payload.password), password ? password : "", "password", error)) {
      sdk->cleanup();
      sdk_close(sdk);
      return NULL;
    }
    payload.ip_mode = 0;
    ok = sdk->modify_net_info(&payload);
    memset(&payload, 0, sizeof(payload));
  }

  guint code = ok ? 0 : (sdk->get_last_error ? sdk->get_last_error() : 0);
  sdk->cleanup();
  sdk_close(sdk);
  if (!ok) {
    g_set_error(error, TVT_SDK_ERROR, TVT_SDK_ERROR_MODIFY,
                "%s rejected the network change (SDK error %u).", function_used, code);
    return NULL;
  }

  TvtSdkModifyResult *result = g_new0(TvtSdkModifyResult, 1);
  result->success = TRUE;
  result->function_used = g_strdup(function_used);
  return result;
}

void
tvt_sdk_modify_result_free(TvtSdkModifyResult *result)
{
  if (!result)
    return;
  g_free(result->function_used);
  g_free(result);
}
