#include "web-uri.h"

char *
tvt_device_web_uri(TvtDevice *device, GError **error)
{
  const char *ip = tvt_device_get_ip(device);
  g_autoptr(GInetAddress) address = g_inet_address_new_from_string(ip);
  if (!address) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "The selected device does not have a valid IP address.");
    return NULL;
  }

  guint port = tvt_device_get_http_port(device);
  if (port == 0)
    port = 80;
  const char *scheme = port == 443 ? "https" : "http";
  g_autofree char *host = g_inet_address_get_family(address) == G_SOCKET_FAMILY_IPV6
                            ? g_strdup_printf("[%s]", ip)
                            : g_strdup(ip);
  if ((port == 80 && g_str_equal(scheme, "http")) ||
      (port == 443 && g_str_equal(scheme, "https")))
    return g_strdup_printf("%s://%s/", scheme, host);
  return g_strdup_printf("%s://%s:%u/", scheme, host, port);
}
