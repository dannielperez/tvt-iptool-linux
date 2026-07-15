core_sources = files(
  'device.c',
  'discovery.c',
  'network.c',
  'sdk-loader.c',
  'web-uri.c',
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

application_source = gui_variant == 'gtk3' ? 'application-gtk3.c' : 'application-gtk4.c'
application_c_args = ['-DTVT_GUI_VARIANT="@0@"'.format(gui_variant)]
if gui_variant == 'gtk4'
  # GTK 4.14 contains a harmless extra semicolon in a public header that
  # -Wpedantic diagnoses. Keep strict warnings enabled for the shared core.
  application_c_args += ['-Wno-pedantic']
endif

executable(
  'tvt-iptool',
  files('main.c', application_source),
  dependencies: [gtk_dep, tvt_core_dep],
  c_args: application_c_args,
  install: true,
)
