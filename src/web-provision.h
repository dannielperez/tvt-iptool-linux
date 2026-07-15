#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define TVT_WEB_PROVISION_ERROR (tvt_web_provision_error_quark())

typedef enum {
  TVT_WEB_PROVISION_ERROR_ARGUMENT,
  TVT_WEB_PROVISION_ERROR_CONNECT,
  TVT_WEB_PROVISION_ERROR_HTTP,
  TVT_WEB_PROVISION_ERROR_AUTH,
  TVT_WEB_PROVISION_ERROR_PROTOCOL,
  TVT_WEB_PROVISION_ERROR_UNSUPPORTED,
} TvtWebProvisionError;

GQuark tvt_web_provision_error_quark(void);

gboolean tvt_web_ipv6_is_stale(const char *address);

gboolean tvt_web_set_network(const char *host,
                             guint16 http_port,
                             const char *mac,
                             const char *password,
                             const char *new_ip,
                             const char *subnet_mask,
                             const char *gateway,
                             gboolean dhcp,
                             GCancellable *cancellable,
                             GError **error);

G_END_DECLS
