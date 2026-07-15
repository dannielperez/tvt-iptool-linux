#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define TVT_SDK_ERROR (tvt_sdk_error_quark())

typedef enum {
  TVT_SDK_ERROR_NOT_FOUND,
  TVT_SDK_ERROR_INCOMPATIBLE,
  TVT_SDK_ERROR_INITIALIZE,
  TVT_SDK_ERROR_MODIFY,
  TVT_SDK_ERROR_ARGUMENT,
} TvtSdkError;

typedef struct {
  gboolean success;
  guint error_code;
  char *function_used;
} TvtSdkModifyResult;

GQuark tvt_sdk_error_quark(void);

gboolean tvt_sdk_is_available(const char *sdk_path, char **detail);
TvtSdkModifyResult *tvt_sdk_modify_ip(const char *sdk_path, const char *mac, const char *password,
                                      const char *new_ip, const char *subnet_mask,
                                      const char *gateway, GError **error);
void tvt_sdk_modify_result_free(TvtSdkModifyResult *result);

G_END_DECLS
