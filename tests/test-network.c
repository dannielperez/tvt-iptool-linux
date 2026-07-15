#include <glib.h>

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
  return g_test_run();
}
