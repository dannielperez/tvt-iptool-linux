#include <glib.h>
#include <string.h>

#include "sdk-loader.h"

static const char *fake_sdk_path;

static void
loads_optional_sdk(void)
{
  g_autofree char *detail = NULL;
  g_assert_true(tvt_sdk_is_available(fake_sdk_path, &detail));
  g_assert_nonnull(strstr(detail, "NET_SDK_ModifyDeviceNetInfo"));
}

static void
rejects_missing_sdk(void)
{
  g_autofree char *detail = NULL;
  g_assert_false(tvt_sdk_is_available("/definitely/not/a/vendor/sdk.so", &detail));
  g_assert_nonnull(detail);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_assert_cmpint(argc, ==, 2);
  fake_sdk_path = argv[1];
  g_test_add_func("/sdk/load", loads_optional_sdk);
  g_test_add_func("/sdk/missing", rejects_missing_sdk);
  return g_test_run();
}
