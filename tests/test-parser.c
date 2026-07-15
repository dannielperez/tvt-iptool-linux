#include <glib.h>

#include "discovery.h"

static const char response[] =
  "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\n\r\n"
  "<multicastSearchResult>"
  "<tcpIp><devName>Front Gate</devName><ipAddr>192.168.10.50</ipAddr>"
  "<mask>255.255.255.0</mask><gateway>192.168.10.1</gateway>"
  "<dns1>1.1.1.1</dns1><macAddr>58:5B:69:11:22:33</macAddr><dhcpSwitch>0</dhcpSwitch></tcpIp>"
  "<port><dataPort>9008</dataPort><httpPort>80</httpPort></port>"
  "<productInfo><productModel>IP-5IRD4S4</productModel><productSeries>TVT_IPC</productSeries>"
  "<softwareVer>V5.2.0</softwareVer><softwareBulildDate>2024-05-20</softwareBulildDate>"
  "<kernelVer>4.19</kernelVer></productInfo>"
  "</multicastSearchResult>";

static void
parses_tvt_response(void)
{
  g_autoptr(GError) error = NULL;
  TvtDevice *device = tvt_device_parse_response(response, sizeof(response) - 1, "192.168.10.50", &error);
  g_assert_no_error(error);
  g_assert_nonnull(device);
  g_assert_cmpstr(tvt_device_get_ip(device), ==, "192.168.10.50");
  g_assert_cmpstr(tvt_device_get_mac(device), ==, "58:5B:69:11:22:33");
  g_assert_cmpstr(tvt_device_get_name(device), ==, "Front Gate");
  g_assert_cmpstr(tvt_device_get_device_type(device), ==, "IPC");
  g_assert_cmpstr(tvt_device_get_model(device), ==, "IP-5IRD4S4");
  g_assert_cmpstr(tvt_device_get_subnet_mask(device), ==, "255.255.255.0");
  g_assert_cmpuint(tvt_device_get_data_port(device), ==, 9008);
  g_assert_cmpuint(tvt_device_get_http_port(device), ==, 80);
  g_assert_false(tvt_device_get_dhcp(device));
  g_object_unref(device);
}

static void
falls_back_to_source_ip(void)
{
  const char xml[] = "<multicastSearchResult><tcpIp><macAddr>AA:BB:CC:DD:EE:FF</macAddr></tcpIp></multicastSearchResult>";
  g_autoptr(GError) error = NULL;
  TvtDevice *device = tvt_device_parse_response(xml, sizeof(xml) - 1, "10.0.0.20", &error);
  g_assert_no_error(error);
  g_assert_cmpstr(tvt_device_get_ip(device), ==, "10.0.0.20");
  g_object_unref(device);
}

static void
rejects_unrelated_payload(void)
{
  g_autoptr(GError) error = NULL;
  TvtDevice *device = tvt_device_parse_response("<root/>", 7, "10.0.0.1", &error);
  g_assert_null(device);
  g_assert_error(error, TVT_DISCOVERY_ERROR, TVT_DISCOVERY_ERROR_PARSE);
}

static void
filters_device_fields(void)
{
  TvtDevice *device = g_object_new(TVT_TYPE_DEVICE,
                                   "ip", "10.0.0.5",
                                   "mac", "AA:BB:CC:DD:EE:FF",
                                   "model", "IP-5IRD4S4",
                                   "device-type", "IPC",
                                   NULL);
  g_assert_true(tvt_device_matches(device, "ird4", "All devices"));
  g_assert_true(tvt_device_matches(device, "aa:bb", "IPC"));
  g_assert_false(tvt_device_matches(device, "aa:bb", "NVR"));
  g_object_unref(device);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/parser/tvt-response", parses_tvt_response);
  g_test_add_func("/parser/source-fallback", falls_back_to_source_ip);
  g_test_add_func("/parser/reject-unrelated", rejects_unrelated_payload);
  g_test_add_func("/device/filter", filters_device_fields);
  return g_test_run();
}
