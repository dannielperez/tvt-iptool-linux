core_sources = files(
  'device.c',
  'discovery.c',
  'network.c',
  'sdk-loader.c',
)

core_deps = [glib_dep, gobject_dep, gio_dep, dl_dep]

tvt_core = static_library(
  'tvt-iptool-core',
  core_sources,
  dependencies: core_deps,
  include_directories: include_directories('.'),
)

tvt_core_dep = declare_dependency(
  link_with: tvt_core,
  dependencies: core_deps,
  include_directories: include_directories('.'),
)

executable(
  'tvt-iptool',
  files('main.c', 'application.c'),
  dependencies: [gtk_dep, tvt_core_dep],
  # GTK 4.14 itself contains a harmless extra semicolon that -Wpedantic
  # diagnoses through a public header. Keep strict warnings for our core.
  c_args: ['-Wno-pedantic'],
  install: true,
)
