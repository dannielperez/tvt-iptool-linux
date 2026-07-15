#include "web-provision.h"

#include <string.h>

#define MAX_HTTP_RESPONSE (1024 * 1024)

static const char public_key[] =
  /* The firmware requires an RSA public key and wraps a session key with it.
   * Network configuration itself uses the nonce-bound proof and returned
   * bearer token, so these endpoints never consume the wrapped session key. */
  "-----BEGIN PUBLIC KEY-----\n"
  "MIGeMA0GCSqGSIb3DQEBAQUAA4GMADCBiAKBgGx+g6v+ZskpyjNS395JltbRy5nB\n"
  "fcggSwKr90hSFJKOkKiYACjJEzUItXwfb0XUTnxdJdUUOpTwgiI9ILMk3dnfLUqa\n"
  "ORjM2XcH/ztXm6EH0Cm9/8PtrBpU9Kybx2U82w/DzX3Ja5N0XEd60x+2ycbfBpsC\n"
  "sKnhXhFIqY1P0EEBAgMBAAE=\n"
  "-----END PUBLIC KEY-----\n";

static const char *nic_fields[] = {
  "dhcpSwitch", "ip", "gateway", "mask", "mac", "mtu",
  "ipV6Switch", "ipV6", "gatewayV6", "subLengthV6",
  "ipv4DnsDhcpSwitch", "dns1", "dns2", "ipv6DnsDhcpSwitch",
  "ipv6Dns1", "ipv6Dns2", "secondIpSwitch", "secondIp", "secondMask",
};

enum { NIC_FIELD_COUNT = G_N_ELEMENTS(nic_fields) };

typedef struct {
  char *id;
  gboolean is_poe;
  gboolean supports_second_ip;
  char *values[NIC_FIELD_COUNT];
} NicConfig;

typedef struct {
  GPtrArray *nics;
  char *default_nic;
  char *poe_mode;
  char *toe_enable;
  char *work_mode;
} NetworkConfig;

G_DEFINE_QUARK(tvt-web-provision-error-quark, tvt_web_provision_error)

static void
secure_clear(char *value)
{
  if (!value)
    return;
  volatile char *cursor = value;
  gsize length = strlen(value);
  while (length-- > 0)
    *cursor++ = 0;
}

static void
nic_config_free(NicConfig *nic)
{
  if (!nic)
    return;
  g_free(nic->id);
  for (guint i = 0; i < NIC_FIELD_COUNT; i++)
    g_free(nic->values[i]);
  g_free(nic);
}

static void
network_config_free(NetworkConfig *config)
{
  if (!config)
    return;
  g_ptr_array_unref(config->nics);
  g_free(config->default_nic);
  g_free(config->poe_mode);
  g_free(config->toe_enable);
  g_free(config->work_mode);
  g_free(config);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NetworkConfig, network_config_free)

static char *
xml_field(const char *xml, const char *name)
{
  g_autofree char *escaped = g_regex_escape_string(name, -1);
  g_autofree char *cdata_pattern = g_strdup_printf(
    "<%s(?:\\s[^>]*)?>\\s*<!\\[CDATA\\[(.*?)\\]\\]>\\s*</%s>", escaped, escaped);
  g_autoptr(GRegex) cdata = g_regex_new(cdata_pattern, G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) match = NULL;
  if (g_regex_match(cdata, xml, 0, &match))
    return g_match_info_fetch(match, 1);

  g_clear_pointer(&match, g_match_info_free);
  g_autofree char *plain_pattern = g_strdup_printf(
    "<%s(?:\\s[^>]*)?>\\s*([^<]*?)\\s*</%s>", escaped, escaped);
  g_autoptr(GRegex) plain = g_regex_new(plain_pattern, G_REGEX_DOTALL, 0, NULL);
  if (g_regex_match(plain, xml, 0, &match)) {
    char *value = g_match_info_fetch(match, 1);
    g_strstrip(value);
    return value;
  }
  return g_strdup("");
}

static char *
xml_attribute(const char *attributes, const char *name)
{
  g_autofree char *escaped = g_regex_escape_string(name, -1);
  g_autofree char *pattern = g_strdup_printf("(?:^|\\s)%s=[\"']([^\"']*)[\"']", escaped);
  g_autoptr(GRegex) regex = g_regex_new(pattern, 0, 0, NULL);
  g_autoptr(GMatchInfo) match = NULL;
  if (g_regex_match(regex, attributes, 0, &match))
    return g_match_info_fetch(match, 1);
  return g_strdup("");
}

static gboolean
xml_succeeded(const char *xml, const char *operation, GError **error)
{
  g_autofree char *status = xml_field(xml, "status");
  if (g_str_equal(status, "success"))
    return TRUE;
  g_autofree char *code = xml_field(xml, "errorCode");
  TvtWebProvisionError error_code = g_str_equal(operation, "doLogin")
    ? TVT_WEB_PROVISION_ERROR_AUTH : TVT_WEB_PROVISION_ERROR_PROTOCOL;
  if (*code)
    g_set_error(error, TVT_WEB_PROVISION_ERROR, error_code,
                "%s failed (errorCode=%s)", operation, code);
  else
    g_set_error(error, TVT_WEB_PROVISION_ERROR, error_code, "%s failed", operation);
  return FALSE;
}

static char *
xml_block(const char *xml, const char *name)
{
  g_autofree char *escaped = g_regex_escape_string(name, -1);
  g_autofree char *pattern = g_strdup_printf(
    "<%s(?:\\s[^>]*)?>(.*?)</%s>", escaped, escaped);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) match = NULL;
  if (g_regex_match(regex, xml, 0, &match))
    return g_match_info_fetch(match, 1);
  return g_strdup("");
}

static char *
cookie_from_headers(const char *headers)
{
  g_auto(GStrv) lines = g_strsplit(headers, "\r\n", -1);
  for (guint i = 0; lines[i]; i++) {
    if (g_ascii_strncasecmp(lines[i], "Set-Cookie:", 11) != 0)
      continue;
    char *value = g_strdup(lines[i] + 11);
    g_strstrip(value);
    char *semicolon = strchr(value, ';');
    if (semicolon)
      *semicolon = 0;
    return value;
  }
  return NULL;
}

static char *
http_post(const char *host, guint16 port, const char *path, const char *cookie,
          const char *body, GCancellable *cancellable, char **response_cookie,
          GError **error)
{
  g_autoptr(GSocketClient) client = g_socket_client_new();
  g_socket_client_set_timeout(client, 4);
  g_autoptr(GError) connect_error = NULL;
  g_autoptr(GSocketConnection) connection = g_socket_client_connect_to_host(
    client, host, port, cancellable, &connect_error);
  if (!connection) {
    g_set_error(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_CONNECT,
                "Cannot reach the device web API at %s:%u: %s",
                host, port, connect_error->message);
    return NULL;
  }

  g_autofree char *cookie_header = cookie
    ? g_strdup_printf("Cookie: %s\r\n", cookie) : g_strdup("");
  g_autofree char *request = g_strdup_printf(
    "POST /%s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Accept: application/xml, text/xml, */*; q=0.01\r\n"
    "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n"
    "X-Requested-With: XMLHttpRequest\r\n"
    "%s"
    "Connection: close\r\n"
    "Content-Length: %zu\r\n\r\n%s",
    path, host, cookie_header,
    strlen(body), body);

  GOutputStream *output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
  gsize written = 0;
  if (!g_output_stream_write_all(output, request, strlen(request), &written,
                                 cancellable, error) ||
      !g_output_stream_flush(output, cancellable, error))
    return NULL;

  GInputStream *input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
  g_autoptr(GByteArray) bytes = g_byte_array_new();
  guint8 buffer[4096];
  while (bytes->len < MAX_HTTP_RESPONSE) {
    gssize count = g_input_stream_read(input, buffer, sizeof(buffer), cancellable, error);
    if (count < 0)
      return NULL;
    if (count == 0)
      break;
    g_byte_array_append(bytes, buffer, (guint)count);
  }
  if (bytes->len >= MAX_HTTP_RESPONSE) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_HTTP,
                        "Device web API response was too large");
    return NULL;
  }
  g_byte_array_append(bytes, (const guint8 *)"", 1);
  char *raw = (char *)bytes->data;
  char *separator = strstr(raw, "\r\n\r\n");
  if (!separator) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_HTTP,
                        "Device returned an invalid HTTP response");
    return NULL;
  }
  if (!g_str_has_prefix(raw, "HTTP/1.1 200") && !g_str_has_prefix(raw, "HTTP/1.0 200")) {
    g_set_error(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_HTTP,
                "Device web API returned %.12s", raw);
    return NULL;
  }
  *separator = 0;
  if (response_cookie)
    *response_cookie = cookie_from_headers(raw);
  return g_strdup(separator + 4);
}

static char *
request_xml(const char *token, const char *content)
{
  return g_strdup_printf(
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
    "<request version=\"1.0\"   systemType=\"NVMS-9000\" clientType=\"WEB\">"
    "<token>%s</token>%s</request>", token ? token : "null", content ? content : "");
}

static char *
login_proof(const char *password, const char *nonce)
{
  g_autofree char *md5 = g_compute_checksum_for_string(G_CHECKSUM_MD5, password, -1);
  for (char *cursor = md5; *cursor; cursor++)
    *cursor = g_ascii_toupper(*cursor);
  g_autofree char *input = g_strdup_printf("%s#%s#%s", md5, nonce, public_key);
  return g_compute_checksum_for_string(G_CHECKSUM_SHA512, input, -1);
}

static NetworkConfig *
parse_network_config(const char *xml, GError **error)
{
  g_autofree char *work_mode = xml_field(xml, "curWorkMode");
  if (g_str_equal(work_mode, "network_fault_tolerance")) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_UNSUPPORTED,
                        "Authenticated fallback does not modify network-fault-tolerance groups");
    return NULL;
  }
  g_autoptr(NetworkConfig) config = g_new0(NetworkConfig, 1);
  config->nics = g_ptr_array_new_with_free_func((GDestroyNotify)nic_config_free);
  config->default_nic = xml_field(xml, "defaultNic");
  config->poe_mode = xml_field(xml, "poeMode");
  config->toe_enable = xml_field(xml, "toeEnable");
  config->work_mode = g_steal_pointer(&work_mode);

  g_autofree char *nics_xml = xml_block(xml, "nicConfigs");
  if (!*nics_xml) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_PROTOCOL,
                        "queryNetCfgV2 returned no NIC configuration");
    return NULL;
  }
  g_autoptr(GRegex) item_regex = g_regex_new(
    "<item\\s+([^>]*)>(.*?)</item>", G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) match = NULL;
  g_regex_match(item_regex, nics_xml, 0, &match);
  while (g_match_info_matches(match)) {
    g_autofree char *attributes = g_match_info_fetch(match, 1);
    g_autofree char *body = g_match_info_fetch(match, 2);
    NicConfig *nic = g_new0(NicConfig, 1);
    nic->id = xml_attribute(attributes, "id");
    g_autofree char *is_poe = xml_attribute(attributes, "isPoe");
    g_autofree char *second = xml_attribute(attributes, "isSupSecondIP");
    nic->is_poe = g_str_equal(is_poe, "true");
    nic->supports_second_ip = g_str_equal(second, "true");
    for (guint i = 0; i < NIC_FIELD_COUNT; i++)
      nic->values[i] = xml_field(body, nic_fields[i]);
    g_ptr_array_add(config->nics, nic);
    g_match_info_next(match, NULL);
  }
  if (config->nics->len == 0) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_PROTOCOL,
                        "queryNetCfgV2 returned an empty NIC list");
    return NULL;
  }
  return g_steal_pointer(&config);
}

static guint
field_index(const char *name)
{
  for (guint i = 0; i < NIC_FIELD_COUNT; i++)
    if (g_str_equal(nic_fields[i], name))
      return i;
  g_assert_not_reached();
}

static void
append_element(GString *xml, const char *name, const char *value)
{
  g_autofree char *escaped = g_markup_escape_text(value ? value : "", -1);
  g_string_append_printf(xml, "<%s>%s</%s>", name, escaped, name);
}

static char *
build_edit_content(NetworkConfig *config, const char *mac,
                   const char *new_ip, const char *mask, const char *gateway,
                   gboolean dhcp, GError **error)
{
  NicConfig *target = NULL;
  guint mac_index = field_index("mac");
  for (guint i = 0; i < config->nics->len; i++) {
    NicConfig *nic = g_ptr_array_index(config->nics, i);
    if (g_ascii_strcasecmp(nic->values[mac_index], mac) == 0)
      target = nic;
  }
  if (!target) {
    g_set_error(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_PROTOCOL,
                "queryNetCfgV2 did not contain the selected MAC %s", mac);
    return NULL;
  }

  guint dhcp_index = field_index("dhcpSwitch");
  guint ip_index = field_index("ip");
  guint gateway_index = field_index("gateway");
  guint mask_index = field_index("mask");
  guint dns_dhcp_index = field_index("ipv4DnsDhcpSwitch");
  guint dns1_index = field_index("dns1");
  guint second_switch_index = field_index("secondIpSwitch");
  g_autoptr(GString) content = g_string_new("<content><nicConfigs>");
  gboolean has_poe = FALSE;
  for (guint i = 0; i < config->nics->len; i++) {
    NicConfig *nic = g_ptr_array_index(config->nics, i);
    has_poe |= nic->is_poe;
    g_autofree char *escaped_id = g_markup_escape_text(nic->id, -1);
    g_string_append_printf(content, "<item id=\"%s\">", escaped_id);
    for (guint field = 0; field < NIC_FIELD_COUNT; field++) {
      if (field >= second_switch_index && !nic->supports_second_ip)
        continue;
      const char *value = nic->values[field];
      if (nic == target) {
        if (field == dhcp_index)
          value = dhcp ? "true" : "false";
        else if (!dhcp && field == ip_index)
          value = new_ip;
        else if (!dhcp && field == gateway_index)
          value = gateway;
        else if (!dhcp && field == mask_index)
          value = mask;
        else if (field == dns_dhcp_index)
          value = dhcp ? "true" : "false";
        else if (!dhcp && field == dns1_index &&
                 (g_str_equal(nic->values[dns_dhcp_index], "true") ||
                  g_str_equal(nic->values[dns1_index], nic->values[gateway_index])))
          value = gateway;
        else if (dhcp && field == second_switch_index)
          value = "false";
      }
      append_element(content, nic_fields[field], value);
    }
    g_string_append(content, "</item>");
  }
  g_string_append(content, "</nicConfigs>");
  append_element(content, "defaultNic", config->default_nic);
  if (has_poe)
    append_element(content, "poeMode", config->poe_mode);
  if (g_str_equal(config->toe_enable, "true"))
    append_element(content, "toeEnable", "true");
  append_element(content, "curWorkMode", config->work_mode);
  g_string_append(content, "</content>");
  return g_string_free(g_steal_pointer(&content), FALSE);
}

gboolean
tvt_web_set_network(const char *host, guint16 http_port, const char *mac,
                    const char *password, const char *new_ip,
                    const char *subnet_mask, const char *gateway, gboolean dhcp,
                    GCancellable *cancellable, GError **error)
{
  if (!host || !*host || !mac || !*mac || !password || !*password) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_ARGUMENT,
                        "Host, MAC address, and administrator password are required");
    return FALSE;
  }
  if (http_port == 0)
    http_port = 80;

  g_autofree char *empty_request = request_xml("null", NULL);
  g_autofree char *cookie = NULL;
  g_autofree char *challenge = http_post(host, http_port, "reqLogin", NULL,
                                         empty_request, cancellable, &cookie, error);
  if (!challenge)
    return FALSE;
  if (!xml_succeeded(challenge, "reqLogin", error))
    return FALSE;
  g_autofree char *nonce = xml_field(challenge, "nonce");
  if (!*nonce) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_PROTOCOL,
                        "reqLogin returned no nonce");
    return FALSE;
  }
  if (!cookie) {
    g_autofree char *session_id = xml_field(challenge, "sessionId");
    g_strdelimit(session_id, "{}", ' ');
    g_strstrip(session_id);
    cookie = g_strdup_printf("sessionId=%s", session_id);
  }

  g_autofree char *proof = login_proof(password, nonce);
  g_autofree char *login_content = g_strdup_printf(
    "<content><userName><![CDATA[admin]]></userName>"
    "<password><![CDATA[%s]]></password><rsaPublic>%s</rsaPublic></content>",
    proof, public_key);
  secure_clear(proof);
  g_autofree char *login_request = request_xml("null", login_content);
  g_autofree char *login_response = http_post(host, http_port, "doLogin", cookie,
                                              login_request, cancellable, NULL, error);
  if (!login_response)
    return FALSE;
  if (!xml_succeeded(login_response, "doLogin", error))
    return FALSE;
  g_autofree char *token = xml_field(login_response, "token");
  if (!*token) {
    g_set_error_literal(error, TVT_WEB_PROVISION_ERROR, TVT_WEB_PROVISION_ERROR_PROTOCOL,
                        "doLogin returned no token");
    return FALSE;
  }

  g_autofree char *query_request = request_xml(token, NULL);
  g_autofree char *query_response = http_post(host, http_port, "queryNetCfgV2", cookie,
                                              query_request, cancellable, NULL, error);
  if (!query_response || !xml_succeeded(query_response, "queryNetCfgV2", error))
    return FALSE;
  g_autoptr(NetworkConfig) config = parse_network_config(query_response, error);
  if (!config)
    return FALSE;
  g_autofree char *edit_content = build_edit_content(config, mac, new_ip, subnet_mask,
                                                     gateway, dhcp, error);
  if (!edit_content)
    return FALSE;
  g_autofree char *edit_request = request_xml(token, edit_content);
  g_autofree char *edit_response = http_post(host, http_port, "editNetCfgV2", cookie,
                                             edit_request, cancellable, NULL, error);
  if (!edit_response)
    return FALSE;
  return xml_succeeded(edit_response, "editNetCfgV2", error);
}
