project(
  'tvt-iptool-linux',
  'c',
  version: '0.2.0',
  license: 'MIT',
  meson_version: '>=0.56.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
  ],
)

gnome = import('gnome')
i18n = import('i18n')
cc = meson.get_compiler('c')

glib_dep = dependency('glib-2.0', version: '>=2.56')
gobject_dep = dependency('gobject-2.0', version: '>=2.56')
gio_dep = dependency('gio-2.0', version: '>=2.56')
gui_variant = get_option('gui')
if gui_variant == 'gtk3'
  gtk_dep = dependency('gtk+-3.0', version: '>=3.22')
else
  gtk_dep = dependency('gtk4', version: '>=4.8')
endif
dl_dep = cc.find_library('dl', required: false)

subdir('src')
subdir('tests')
subdir('data')

summary({
  'Version': meson.project_version(),
  'GUI variant': gui_variant,
  'GTK': gtk_dep.version(),
  'Vendor SDK bundled': false,
}, section: 'TVT IPTool for Linux')
