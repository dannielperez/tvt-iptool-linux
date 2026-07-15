#include <glib.h>

#include "network.h"

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

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/network/valid", valid_plan);
  g_test_add_func("/network/non-contiguous-mask", rejects_non_contiguous_mask);
  g_test_add_func("/network/network-broadcast", rejects_network_and_broadcast);
  g_test_add_func("/network/off-subnet-warning", warns_for_off_subnet_gateway);
  g_test_add_func("/network/special-addresses", rejects_special_addresses);
  return g_test_run();
}
