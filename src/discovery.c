#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "discovery.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SSDP_ADDRESS "239.255.255.250"
#define SSDP_PORT 1900
#define TVT_L2_SEND_ADDRESS "234.55.55.55"
#define TVT_L2_RECV_ADDRESS "234.55.55.56"
#define TVT_L2_PORT 23456
#define TVT_L2_PACKET_SIZE 140

static const guint8 tvt_l2_search_request[TVT_L2_PACKET_SIZE] = {
  0x4d, 0x48, 0x45, 0x44, /* MHED */
  0x08, 0x00, 0x01, 0x00, /* protocol version 0x00010008 */
  0x01, 0x00, 0x00, 0x00, /* search request */
};

static const char search_request[] =
  "M-SEARCH * HTTP/1.1\r\n"
  "HOST:239.255.255.250:1900\r\n"
  "Man:\"ssdp:discover\"\r\n"
  "ST:urn:schemas-upnp-org:service:EmbeddedNetDeviceControl:1\r\n"
  "MX:3\r\n\r\n";

typedef struct {
  GPtrArray *elements;
  GString *text;
  GHashTable *values;
  gboolean saw_root;
} ParseState;

G_DEFINE_QUARK(tvt-discovery-error-quark, tvt_discovery_error)

static char *
path_for(ParseState *state)
{
  GString *path = g_string_new(NULL);
  for (guint i = 0; i < state->elements->len; i++) {
    if (i > 0)
      g_string_append_c(path, '/');
    g_string_append(path, g_ptr_array_index(state->elements, i));
  }
  return g_string_free(path, FALSE);
}

static void
on_start_element(GMarkupParseContext *context, const char *name, const char **attribute_names,
                 const char **attribute_values, gpointer user_data, GError **error)
{
  ParseState *state = user_data;
  (void)context;
  (void)attribute_names;
  (void)attribute_values;
  (void)error;
  g_ptr_array_add(state->elements, g_strdup(name));
  g_string_truncate(state->text, 0);
  if (state->elements->len == 1 && g_str_equal(name, "multicastSearchResult"))
    state->saw_root = TRUE;
}

static void
on_text(GMarkupParseContext *context, const char *text, gsize text_len, gpointer user_data, GError **error)
{
  ParseState *state = user_data;
  (void)context;
  (void)error;
  g_string_append_len(state->text, text, text_len);
}

static void
on_end_element(GMarkupParseContext *context, const char *name, gpointer user_data, GError **error)
{
  ParseState *state = user_data;
  (void)context;
  (void)name;
  (void)error;

  g_autofree char *path = path_for(state);
  g_autofree char *value = g_strdup(state->text->str);
  g_strstrip(value);
  if (*value)
    g_hash_table_replace(state->values, g_strdup(path), g_strdup(value));

  if (state->elements->len > 0)
    g_ptr_array_remove_index(state->elements, state->elements->len - 1);
  g_string_truncate(state->text, 0);
}

static const char *
value(ParseState *state, const char *suffix)
{
  g_autofree char *key = g_strdup_printf("multicastSearchResult/%s", suffix);
  const char *found = g_hash_table_lookup(state->values, key);
  return found ? found : "";
}

static const char *
infer_type(const char *series, const char *model)
{
  g_autofree char *series_upper = g_ascii_strup(series, -1);
  g_autofree char *model_upper = g_ascii_strup(model, -1);
  if (strstr(series_upper, "NVR")) return "NVR";
  if (strstr(series_upper, "IPC")) return "IPC";
  if (strstr(series_upper, "MDVR")) return "MDVR";
  if (strstr(series_upper, "DVR")) return "DVR";
  if (strstr(series_upper, "STORAGE")) return "Storage";
  if (strstr(series_upper, "DECODER")) return "Decoder";
  if (strstr(series_upper, "NETKEYBOARD")) return "Network keyboard";
  if (g_str_has_prefix(model_upper, "IP-") || g_str_has_prefix(model_upper, "TD-9")) return "IPC";
  if (g_str_has_prefix(model_upper, "TD-") && strstr(model_upper, "TS")) return "NVR";
  return "Unknown";
}

static guint16
read_le16(const guint8 *data)
{
  return (guint16)data[0] | ((guint16)data[1] << 8);
}

static guint32
read_le32(const guint8 *data)
{
  return (guint32)data[0] | ((guint32)data[1] << 8) |
         ((guint32)data[2] << 16) | ((guint32)data[3] << 24);
}

static char *
format_l2_ipv4(const guint8 *address)
{
  if (address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0)
    return g_strdup("");
  return g_strdup_printf("%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
}

static const char *
l2_device_type(guint32 type)
{
  switch (type) {
    case 2: return "NVR";
    case 3: return "DVR";
    case 4: return "IPC";
    case 5: return "Storage";
    case 6: return "MDVR";
    case 8: return "Decoder";
    case 0x10101: return "Network keyboard";
    default: return "Unknown";
  }
}

static char *
read_l2_text(const guint8 *data, gsize capacity)
{
  gsize length = 0;
  while (length < capacity && data[length] != 0)
    length++;
  char *text = g_strndup((const char *)data, length);
  if (!g_utf8_validate(text, -1, NULL)) {
    g_free(text);
    return g_strdup("");
  }
  g_strstrip(text);
  return text;
}

static TvtDevice *
parse_l2_response(const guint8 *data, gsize length, GError **error)
{
  if (length < TVT_L2_PACKET_SIZE || memcmp(data, "MHED", 4) != 0 || read_le32(data + 8) != 2) {
    g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_PARSE,
                "Response was not a TVT Layer-2 discovery result");
    return NULL;
  }

  g_autofree char *ip = format_l2_ipv4(data + 0x28);
  g_autofree char *mask = format_l2_ipv4(data + 0x2c);
  g_autofree char *gateway = format_l2_ipv4(data + 0x30);
  g_autofree char *name = read_l2_text(data + 0x0c, 20);
  g_autofree char *mac = g_strdup_printf("%02X:%02X:%02X:%02X:%02X:%02X",
                                         data[0x20], data[0x21], data[0x22],
                                         data[0x23], data[0x24], data[0x25]);
  guint32 type = read_le32(data + 0x40);
  return g_object_new(TVT_TYPE_DEVICE,
                      "ip", ip,
                      "mac", mac,
                      "name", *name ? name : "TVT Layer-2 device",
                      "device-type", l2_device_type(type),
                      "subnet-mask", mask,
                      "gateway", gateway,
                      "data-port", (guint)read_le16(data + 0x26),
                      "http-port", (guint)read_le16(data + 0x3c),
                      NULL);
}

TvtDevice *
tvt_device_parse_response(const char *data, gsize length, const char *source_ip, GError **error)
{
  if (length >= 4 && memcmp(data, "MHED", 4) == 0)
    return parse_l2_response((const guint8 *)data, length, error);

  const char *xml = g_strstr_len(data, length, "<multicastSearchResult");
  if (!xml) {
    g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_PARSE,
                "Response did not contain multicastSearchResult XML");
    return NULL;
  }

  ParseState state = {
    .elements = g_ptr_array_new_with_free_func(g_free),
    .text = g_string_new(NULL),
    .values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free),
  };
  const GMarkupParser parser = {
    .start_element = on_start_element,
    .end_element = on_end_element,
    .text = on_text,
  };
  g_autoptr(GMarkupParseContext) context = g_markup_parse_context_new(&parser, 0, &state, NULL);
  gsize xml_length = length - (gsize)(xml - data);
  gboolean parsed = g_markup_parse_context_parse(context, xml, xml_length, error) &&
                    g_markup_parse_context_end_parse(context, error);

  TvtDevice *device = NULL;
  if (parsed && state.saw_root) {
    const char *ip = value(&state, "tcpIp/ipAddr");
    const char *name = value(&state, "tcpIp/devName");
    const char *product_name = value(&state, "productInfo/devName");
    const char *model = value(&state, "productInfo/productModel");
    const char *series = value(&state, "productInfo/productSeries");
    const char *mask = value(&state, "tcpIp/mask");
    const char *mask_addr = value(&state, "tcpIp/maskAddr");
    const char *dhcp = value(&state, "tcpIp/dhcpSwitch");
    device = g_object_new(
      TVT_TYPE_DEVICE,
      "ip", *ip ? ip : (source_ip ? source_ip : ""),
      "mac", value(&state, "tcpIp/macAddr"),
      "name", *name ? name : product_name,
      "device-type", infer_type(series, model),
      "model", model,
      "series", series,
      "firmware", value(&state, "productInfo/softwareVer"),
      "build-date", value(&state, "productInfo/softwareBulildDate"),
      "kernel", value(&state, "productInfo/kernelVer"),
      "subnet-mask", *mask ? mask : mask_addr,
      "gateway", value(&state, "tcpIp/gateway"),
      "dns1", value(&state, "tcpIp/dns1"),
      "dns2", value(&state, "tcpIp/dns2"),
      "data-port", (guint)g_ascii_strtoull(value(&state, "port/dataPort"), NULL, 10),
      "http-port", (guint)g_ascii_strtoull(value(&state, "port/httpPort"), NULL, 10),
      "dhcp", g_str_equal(dhcp, "1") || g_ascii_strcasecmp(dhcp, "true") == 0,
      NULL);
  } else if (parsed) {
    g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_PARSE,
                "XML root was not multicastSearchResult");
  }

  g_ptr_array_unref(state.elements);
  g_string_free(state.text, TRUE);
  g_hash_table_unref(state.values);
  return device;
}

static gint
compare_devices(gconstpointer left, gconstpointer right)
{
  const TvtDevice *a = *(TvtDevice * const *)left;
  const TvtDevice *b = *(TvtDevice * const *)right;
  int type_order = g_strcmp0(tvt_device_get_device_type((TvtDevice *)a),
                             tvt_device_get_device_type((TvtDevice *)b));
  return type_order ? type_order : g_strcmp0(tvt_device_get_ip((TvtDevice *)a),
                                             tvt_device_get_ip((TvtDevice *)b));
}

GPtrArray *
tvt_discover_lan(const TvtDiscoveryOptions *options, GCancellable *cancellable, GError **error)
{
  const guint timeout_ms = options && options->timeout_ms ? options->timeout_ms : 1500;
  const guint retries = options && options->retries ? options->retries : 2;
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_SOCKET,
                "Cannot create discovery socket: %s", g_strerror(errno));
    return NULL;
  }

  int enabled = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

  struct sockaddr_in local = { 0 };
  local.sin_family = AF_INET;
  local.sin_port = htons(TVT_L2_PORT);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  struct in_addr interface_address = { 0 };
  interface_address.s_addr = htonl(INADDR_ANY);
  if (options && options->bind_address && *options->bind_address) {
    if (inet_pton(AF_INET, options->bind_address, &interface_address) != 1) {
      g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_BIND,
                  "Invalid bind address: %s", options->bind_address);
      close(fd);
      return NULL;
    }
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &interface_address, sizeof(interface_address));
  }
  if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
    g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_BIND,
                "Cannot bind discovery socket: %s", g_strerror(errno));
    close(fd);
    return NULL;
  }

  struct ip_mreq membership = { 0 };
  inet_pton(AF_INET, TVT_L2_RECV_ADDRESS, &membership.imr_multiaddr);
  membership.imr_interface = interface_address;
  if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0) {
    g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_BIND,
                "Cannot join TVT Layer-2 discovery group: %s", g_strerror(errno));
    close(fd);
    return NULL;
  }

  unsigned char ttl = 5;
  setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  struct sockaddr_in ssdp_destination = { 0 };
  ssdp_destination.sin_family = AF_INET;
  ssdp_destination.sin_port = htons(SSDP_PORT);
  inet_pton(AF_INET, SSDP_ADDRESS, &ssdp_destination.sin_addr);
  struct sockaddr_in l2_destination = { 0 };
  l2_destination.sin_family = AF_INET;
  l2_destination.sin_port = htons(TVT_L2_PORT);
  inet_pton(AF_INET, TVT_L2_SEND_ADDRESS, &l2_destination.sin_addr);

  GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
  for (guint attempt = 0; attempt < retries; attempt++) {
    if (cancellable && g_cancellable_set_error_if_cancelled(cancellable, error))
      break;
    ssize_t l2_sent = sendto(fd, tvt_l2_search_request, sizeof(tvt_l2_search_request), 0,
                             (struct sockaddr *)&l2_destination, sizeof(l2_destination));
    ssize_t ssdp_sent = sendto(fd, search_request, sizeof(search_request) - 1, 0,
                               (struct sockaddr *)&ssdp_destination, sizeof(ssdp_destination));
    if (l2_sent < 0 && ssdp_sent < 0) {
      g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_SEND,
                  "Cannot send discovery request: %s", g_strerror(errno));
      break;
    }

    gint64 deadline = g_get_monotonic_time() + ((gint64)timeout_ms * 1000);
    while (g_get_monotonic_time() < deadline) {
      if (cancellable && g_cancellable_set_error_if_cancelled(cancellable, error))
        break;
      gint64 remaining_us = deadline - g_get_monotonic_time();
      int wait_ms = (int)MIN((gint64)100, MAX((gint64)1, remaining_us / 1000));
      struct pollfd pfd = { .fd = fd, .events = POLLIN };
      int ready = poll(&pfd, 1, wait_ms);
      if (ready < 0 && errno != EINTR) {
        g_set_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_SOCKET,
                    "Discovery receive failed: %s", g_strerror(errno));
        break;
      }
      if (ready <= 0 || !(pfd.revents & POLLIN))
        continue;

      char buffer[65536];
      struct sockaddr_in source = {0};
      socklen_t source_len = sizeof(source);
      ssize_t received = recvfrom(fd, buffer, sizeof(buffer), 0,
                                  (struct sockaddr *)&source, &source_len);
      if (received <= 0)
        continue;
      char source_ip[INET_ADDRSTRLEN] = {0};
      inet_ntop(AF_INET, &source.sin_addr, source_ip, sizeof(source_ip));
      g_autoptr(GError) parse_error = NULL;
      TvtDevice *device = tvt_device_parse_response(buffer, (gsize)received, source_ip, &parse_error);
      if (!device)
        continue;
      const char *identity = *tvt_device_get_mac(device) ? tvt_device_get_mac(device) : tvt_device_get_ip(device);
      if (!g_hash_table_contains(seen, identity))
        g_hash_table_insert(seen, g_strdup(identity), device);
      else
        g_object_unref(device);
    }
    if (error && *error)
      break;
  }
  close(fd);

  if (error && *error) {
    g_hash_table_unref(seen);
    return NULL;
  }

  GPtrArray *devices = g_ptr_array_new_with_free_func(g_object_unref);
  GHashTableIter iter;
  gpointer item;
  g_hash_table_iter_init(&iter, seen);
  while (g_hash_table_iter_next(&iter, NULL, &item))
    g_ptr_array_add(devices, g_object_ref(item));
  g_hash_table_unref(seen);
  g_ptr_array_sort(devices, compare_devices);
  return devices;
}
