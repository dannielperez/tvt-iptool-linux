#include <glib.h>

#include "l2-provision.h"
#include "network.h"
#include "web-uri.h"

static void
valid_plan(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *warning = NULL;
  g_assert_true(tvt_network_validate("192.168.20.50", "255.255.255.0", "192.168.20.1", &warning, &error));
  g_assert_no_error(error);
  g_assert_null(warning);
}

static void
rejects_non_contiguous_mask(void)
{
  g_autoptr(GError) error = NULL;
  g_assert_false(tvt_network_validate("192.168.20.50", "255.0.255.0", "192.168.20.1", NULL, &error));
  g_assert_error(error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_MASK);
}

static void
rejects_network_and_broadcast(void)
{
  g_autoptr(GError) network_error = NULL;
  g_assert_false(tvt_network_validate("192.168.20.0", "255.255.255.0", "192.168.20.1", NULL, &network_error));
  g_assert_error(network_error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_NETWORK_ADDRESS);

  g_autoptr(GError) broadcast_error = NULL;
  g_assert_false(tvt_network_validate("192.168.20.255", "255.255.255.0", "192.168.20.1", NULL, &broadcast_error));
  g_assert_error(broadcast_error, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_BROADCAST_ADDRESS);
}

static void
warns_for_off_subnet_gateway(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *warning = NULL;
  g_assert_true(tvt_network_validate("10.20.30.50", "255.255.255.0", "10.20.31.1", &warning, &error));
  g_assert_no_error(error);
  g_assert_nonnull(warning);
}

static void
rejects_special_addresses(void)
{
  g_autoptr(GError) loopback = NULL;
  g_assert_false(tvt_network_validate("127.0.0.2", "255.255.255.0", "127.0.0.1", NULL, &loopback));
  g_assert_error(loopback, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_IP);

  g_autoptr(GError) link_local = NULL;
  g_assert_false(tvt_network_validate("169.254.1.2", "255.255.0.0", "169.254.1.1", NULL, &link_local));
  g_assert_error(link_local, TVT_NETWORK_ERROR, TVT_NETWORK_ERROR_INVALID_IP);
}

static void
builds_device_web_uris(void)
{
  TvtDevice *http = g_object_new(TVT_TYPE_DEVICE, "ip", "192.168.1.20", "http-port", 80, NULL);
  TvtDevice *custom = g_object_new(TVT_TYPE_DEVICE, "ip", "10.0.0.9", "http-port", 8080, NULL);
  TvtDevice *https = g_object_new(TVT_TYPE_DEVICE, "ip", "10.0.0.10", "http-port", 443, NULL);
  TvtDevice *ipv6 = g_object_new(TVT_TYPE_DEVICE, "ip", "2001:db8::20", "http-port", 8080, NULL);
  g_autoptr(GError) error = NULL;
  g_autofree char *http_uri = tvt_device_web_uri(http, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(http_uri, ==, "http://192.168.1.20/");
  g_autofree char *custom_uri = tvt_device_web_uri(custom, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(custom_uri, ==, "http://10.0.0.9:8080/");
  g_autofree char *https_uri = tvt_device_web_uri(https, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(https_uri, ==, "https://10.0.0.10/");
  g_autofree char *ipv6_uri = tvt_device_web_uri(ipv6, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(ipv6_uri, ==, "http://[2001:db8::20]:8080/");
  g_object_unref(http);
  g_object_unref(custom);
  g_object_unref(https);
  g_object_unref(ipv6);
}

static void
builds_vendor_compatible_set_ip_packet(void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) request = tvt_l2_build_set_ip_request(
    0x0001000b, "58:5B:69:52:28:71", "admin123", "10.160.35.251",
    "255.255.255.0", "10.160.35.1", FALSE, &error);
  g_assert_no_error(error);
  g_assert_nonnull(request);
  gsize size = 0;
  const guint8 *packet = g_bytes_get_data(request, &size);
  g_assert_cmpuint(size, ==, TVT_L2_PROVISION_PACKET_SIZE);
  const guint8 expected_header[] = { 'M', 'H', 'E', 'D', 0x0b, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00 };
  const guint8 expected_mac[] = { 0x58, 0x5b, 0x69, 0x52, 0x28, 0x71 };
  const guint8 expected_ip[] = { 10, 160, 35, 251 };
  const guint8 expected_mask[] = { 255, 255, 255, 0 };
  const guint8 expected_gateway[] = { 10, 160, 35, 1 };
  g_assert_cmpmem(packet, sizeof(expected_header), expected_header, sizeof(expected_header));
  g_assert_cmpmem(packet + 0x20, sizeof(expected_mac), expected_mac, sizeof(expected_mac));
  g_assert_cmpmem(packet + 0x28, sizeof(expected_ip), expected_ip, sizeof(expected_ip));
  g_assert_cmpmem(packet + 0x2c, sizeof(expected_mask), expected_mask, sizeof(expected_mask));
  g_assert_cmpmem(packet + 0x30, sizeof(expected_gateway), expected_gateway, sizeof(expected_gateway));
  g_assert_cmpmem(packet + 0x54, strlen("YWRtaW4xMjM="),
                  "YWRtaW4xMjM=", strlen("YWRtaW4xMjM="));
  g_assert_cmpuint(packet[0x8a], ==, 0);
}

static void
validates_set_ip_packet_arguments(void)
{
  g_autoptr(GError) mac_error = NULL;
  g_autoptr(GBytes) invalid_mac = tvt_l2_build_set_ip_request(
    0x00010008, "not-a-mac", "password", "192.168.1.2", "255.255.255.0",
    "192.168.1.1", FALSE, &mac_error);
  g_assert_null(invalid_mac);
  g_assert_error(mac_error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_ARGUMENT);

  g_autoptr(GError) password_error = NULL;
  g_autoptr(GBytes) long_password = tvt_l2_build_set_ip_request(
    0x00010008, "58:5B:69:52:28:71", "1234567890123456789012", "192.168.1.2",
    "255.255.255.0", "192.168.1.1", FALSE, &password_error);
  g_assert_null(long_password);
  g_assert_error(password_error, TVT_L2_PROVISION_ERROR, TVT_L2_PROVISION_ERROR_ARGUMENT);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/network/valid", valid_plan);
  g_test_add_func("/network/non-contiguous-mask", rejects_non_contiguous_mask);
  g_test_add_func("/network/network-broadcast", rejects_network_and_broadcast);
  g_test_add_func("/network/off-subnet-warning", warns_for_off_subnet_gateway);
  g_test_add_func("/network/special-addresses", rejects_special_addresses);
  g_test_add_func("/network/web-uri", builds_device_web_uris);
  g_test_add_func("/network/l2-set-ip-packet", builds_vendor_compatible_set_ip_packet);
  g_test_add_func("/network/l2-set-ip-arguments", validates_set_ip_packet_arguments);
  return g_test_run();
}
