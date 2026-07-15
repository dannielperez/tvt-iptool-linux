#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define TVT_L2_PROVISION_PACKET_SIZE 140

#define TVT_L2_PROVISION_ERROR (tvt_l2_provision_error_quark())

typedef enum {
  TVT_L2_PROVISION_ERROR_ARGUMENT,
  TVT_L2_PROVISION_ERROR_SOCKET,
  TVT_L2_PROVISION_ERROR_SEND,
} TvtL2ProvisionError;

GQuark tvt_l2_provision_error_quark(void);

GBytes *tvt_l2_build_set_ip_request(const char *mac, const char *password,
                                    const char *new_ip, const char *subnet_mask,
                                    const char *gateway, gboolean dhcp,
                                    GError **error);

gboolean tvt_l2_send_set_ip(const char *bind_address, const char *mac,
                            const char *password, const char *new_ip,
                            const char *subnet_mask, const char *gateway,
                            gboolean dhcp, GError **error);

G_END_DECLS
