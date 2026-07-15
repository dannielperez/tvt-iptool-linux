#pragma once

#include <gio/gio.h>

#include "device.h"

G_BEGIN_DECLS

#define TVT_DISCOVERY_ERROR (tvt_discovery_error_quark())

typedef enum {
  TVT_DISCOVERY_ERROR_SOCKET,
  TVT_DISCOVERY_ERROR_BIND,
  TVT_DISCOVERY_ERROR_SEND,
  TVT_DISCOVERY_ERROR_PARSE,
} TvtDiscoveryError;

typedef struct {
  guint timeout_ms;
  guint retries;
  const char *bind_address;
} TvtDiscoveryOptions;

GQuark tvt_discovery_error_quark(void);

TvtDevice *tvt_device_parse_response(const char *data, gsize length, const char *source_ip, GError **error);
GPtrArray *tvt_discover_lan(const TvtDiscoveryOptions *options, GCancellable *cancellable, GError **error);

G_END_DECLS
