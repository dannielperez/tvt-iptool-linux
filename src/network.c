#include "network.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>

G_DEFINE_QUARK(tvt-network-error-quark, tvt_network_error)

static gboolean
parse_ipv4(const char *text, struct in_addr *address)
{
  return text && *text && inet_pton(AF_INET, text, address) == 1;
}

static gboolean
is_usable_unicast(guint32 host_order)
{
  guint first = host_order >> 24;
  return host_order != 0 && first >= 1 && first <= 223 && first != 127 &&
         !(first == 169 && ((host_order >> 16) & 0xff) == 254);
}

gboolean
tvt_network_validate(const char *ip, const char *subnet_mask, const char *gateway,
                     char **warning, GError **error)
{
  struct in_addr ip_addr = {0};
  struct in_addr mask_addr = {0};
  struct in_addr gateway_addr = {0};
  if (warning)
    *warning = NULL;

  if (!parse_ipv4(ip, &ip_addr) || !is_usable_unicast(ntohl(ip_addr.s_addr))) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_IP,
                "Enter a usable IPv4 address (unicast, non-loopback, and non-link-local).");
    return FALSE;
  }
  if (!parse_ipv4(subnet_mask, &mask_addr)) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_MASK,
                "Enter a valid IPv4 subnet mask.");
    return FALSE;
  }

  guint32 mask = ntohl(mask_addr.s_addr);
  guint32 inverse_mask = ~mask;
  if (mask == 0 || mask == UINT32_MAX || (inverse_mask & (inverse_mask + 1)) != 0) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_MASK,
                "The subnet mask must contain contiguous network bits and leave usable host bits.");
    return FALSE;
  }
  if (!parse_ipv4(gateway, &gateway_addr) || !is_usable_unicast(ntohl(gateway_addr.s_addr))) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_GATEWAY,
                "Enter a usable IPv4 default gateway.");
    return FALSE;
  }

  guint32 ip_host = ntohl(ip_addr.s_addr);
  guint32 gateway_host = ntohl(gateway_addr.s_addr);
  guint32 host_part = ip_host & inverse_mask;
  if (host_part == 0) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_NETWORK_ADDRESS,
                "%s is the network address for mask %s.", ip, subnet_mask);
    return FALSE;
  }
  if (host_part == inverse_mask) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_BROADCAST_ADDRESS,
                "%s is the broadcast address for mask %s.", ip, subnet_mask);
    return FALSE;
  }

  guint32 gateway_part = gateway_host & inverse_mask;
  if (gateway_part == 0 || gateway_part == inverse_mask) {
    g_set_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_GATEWAY,
                "The gateway cannot be the network or broadcast address.");
    return FALSE;
  }

  if ((ip_host & mask) != (gateway_host & mask) && warning) {
    *warning = g_strdup("The gateway is outside the subnet defined by the new IP and mask. "
                        "Most camera deployments require the gateway to be in the same subnet.");
  }
  return TRUE;
}
