parser_test = executable(
  'test-parser',
  'test-parser.c',
  dependencies: [tvt_core_dep],
)
test('parser', parser_test)

network_test = executable(
  'test-network',
  'test-network.c',
  dependencies: [tvt_core_dep],
)
test('network-validation', network_test)

fake_sdk = shared_module(
  'fake-dvrnetsdk',
  'fake-sdk.c',
  install: false,
)

sdk_loader_test = executable(
  'test-sdk-loader',
  'test-sdk-loader.c',
  dependencies: [tvt_core_dep],
)
test('sdk-runtime-loader', sdk_loader_test, args: fake_sdk.full_path())

test(
  'no-vendor-sdk-artifacts',
  find_program('check-no-vendor-sdk.sh'),
  args: meson.project_source_root(),
)
