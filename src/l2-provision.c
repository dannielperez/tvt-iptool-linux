#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "l2-provision.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TVT_L2_SEND_ADDRESS "234.55.55.55"
#define TVT_L2_PORT 23456
#define TVT_L2_PASSWORD_OFFSET 0x54
#define TVT_L2_PASSWORD_CAPACITY 28

G_DEFINE_QUARK(tvt-l2-provision-error-quark, tvt_l2_provision_error)

static void
secure_clear(gpointer data, gsize length)
{
  volatile guint8 *bytes = data;
  while (length-- > 0)
    *bytes++ = 0;
}

static void
secure_packet_free(gpointer data)
{
  secure_clear(data, TVT_L2_PROVISION_PACKET_SIZE);
  g_free(data);
}

static gboolean
parse_mac(const char *text, guint8 output[6])
{
  if (!text)
    return FALSE;
  g_auto(GStrv) parts = g_strsplit(text, ":", -1);
  if (g_strv_length(parts) != 6)
    return FALSE;
  for (guint i = 0; i < 6; i++) {
    if (strlen(parts[i]) != 2 || !g_ascii_isxdigit(parts[i][0]) ||
        !g_ascii_isxdigit(parts[i][1]))
      return FALSE;
    char *end = NULL;
    guint64 value = g_ascii_strtoull(parts[i], &end, 16);
    if (!end || *end || value > 0xff)
      return FALSE;
    output[i] = (guint8)value;
  }
  return TRUE;
}

static gboolean
write_ipv4(guint8 packet[TVT_L2_PROVISION_PACKET_SIZE], gsize offset,
           const char *value, const char *field, GError **error)
{
  struct in_addr address = {0};
  if (!value || inet_pton(AF_INET, value, &address) != 1) {
    g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_ARGUMENT,
                "Invalid %s: %s", field, value ? value : "");
    return FALSE;
  }
  memcpy(packet + offset, &address, sizeof(address));
  return TRUE;
}

static void
write_le32(guint8 *destination, guint32 value)
{
  destination[0] = (guint8)value;
  destination[1] = (guint8)(value >> 8);
  destination[2] = (guint8)(value >> 16);
  destination[3] = (guint8)(value >> 24);
}

GBytes *
tvt_l2_build_set_ip_request(guint32 protocol_version,
                            const char *mac, const char *password,
                            const char *new_ip, const char *subnet_mask,
                            const char *gateway, gboolean dhcp, GError **error)
{
  guint8 packet[TVT_L2_PROVISION_PACKET_SIZE] = {0};
  guint8 mac_bytes[6] = {0};
  if (!parse_mac(mac, mac_bytes)) {
    g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_ARGUMENT,
                "Invalid device MAC address: %s", mac ? mac : "");
    return NULL;
  }

  memcpy(packet, "MHED", 4);
  if (protocol_version == 0)
    protocol_version = 0x00010008;
  write_le32(packet + 4, protocol_version);
  packet[8] = 0x03;
  memcpy(packet + 0x20, mac_bytes, sizeof(mac_bytes));
  if (!write_ipv4(packet, 0x28, new_ip, "IP address", error) ||
      !write_ipv4(packet, 0x2c, subnet_mask, "subnet mask", error) ||
      !write_ipv4(packet, 0x30, gateway, "gateway", error))
    return NULL;

  const char *secret = password ? password : "";
  gsize secret_length = strlen(secret);
  g_autofree char *encoded = g_base64_encode((const guchar *)secret, secret_length);
  gsize encoded_length = strlen(encoded);
  if (encoded_length > TVT_L2_PASSWORD_CAPACITY) {
    secure_clear(encoded, encoded_length);
    g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_ARGUMENT,
                "Administrator password is too long for TVT Layer-2 provisioning (maximum 21 bytes).");
    return NULL;
  }
  memcpy(packet + TVT_L2_PASSWORD_OFFSET, encoded, encoded_length);
  secure_clear(encoded, encoded_length);
  packet[0x8a] = dhcp ? 1 : 0;
  guint8 *owned_packet = g_malloc(sizeof(packet));
  memcpy(owned_packet, packet, sizeof(packet));
  secure_clear(packet, sizeof(packet));
  return g_bytes_new_with_free_func(owned_packet, sizeof(packet),
                                    secure_packet_free, owned_packet);
}

gboolean
tvt_l2_send_set_ip(const char *bind_address, guint32 protocol_version,
                   const char *mac,
                   const char *password, const char *new_ip,
                   const char *subnet_mask, const char *gateway,
                   gboolean dhcp, GError **error)
{
  g_autoptr(GBytes) request = tvt_l2_build_set_ip_request(
    protocol_version, mac, password, new_ip, subnet_mask, gateway, dhcp, error);
  if (!request)
    return FALSE;

  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_SOCKET,
                "Cannot create TVT provisioning socket: %s", g_strerror(errno));
    return FALSE;
  }

  struct in_addr interface_address = { .s_addr = htonl(INADDR_ANY) };
  if (bind_address && *bind_address) {
    if (inet_pton(AF_INET, bind_address, &interface_address) != 1) {
      g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_ARGUMENT,
                  "Invalid bind address: %s", bind_address);
      close(fd);
      return FALSE;
    }
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &interface_address,
                   sizeof(interface_address)) < 0) {
      g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_SOCKET,
                  "Cannot select the TVT provisioning interface: %s", g_strerror(errno));
      close(fd);
      return FALSE;
    }
  }

  unsigned char ttl = 5;
  setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  struct sockaddr_in destination = {0};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(TVT_L2_PORT);
  inet_pton(AF_INET, TVT_L2_SEND_ADDRESS, &destination.sin_addr);
  gsize request_size = 0;
  gconstpointer request_data = g_bytes_get_data(request, &request_size);
  gboolean sent = FALSE;
  int last_error = 0;
  for (guint attempt = 0; attempt < 3; attempt++) {
    ssize_t count = sendto(fd, request_data, request_size, 0,
                           (struct sockaddr *)&destination, sizeof(destination));
    if (count == (ssize_t)request_size)
      sent = TRUE;
    else
      last_error = count < 0 ? errno : EIO;
    if (attempt < 2)
      g_usleep(100000);
  }
  close(fd);
  if (!sent) {
    g_set_error(error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_SEND,
                "Cannot send TVT Layer-2 set-IP request: %s", g_strerror(last_error));
    return FALSE;
  }
  return TRUE;
}
