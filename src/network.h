#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define TVT_NETWORK_ERROR (tvt_network_error_quark())

typedef enum {
  TVT_NETWORK_ERROR_INVALID_IP,
  TVT_NETWORK_ERROR_INVALID_MASK,
  TVT_NETWORK_ERROR_INVALID_GATEWAY,
  TVT_NETWORK_ERROR_NETWORK_ADDRESS,
  TVT_NETWORK_ERROR_BROADCAST_ADDRESS,
} TvtNetworkError;

GQuark tvt_network_error_quark(void);

gboolean tvt_network_validate(const char *ip, const char *subnet_mask, const char *gateway,
                              char **warning, GError **error);

G_END_DECLS
