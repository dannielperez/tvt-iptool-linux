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

GQuark tvt_sdk_error_quark(void);

gboolean tvt_sdk_is_available(const char *sdk_path, char **detail);

G_END_DECLS
