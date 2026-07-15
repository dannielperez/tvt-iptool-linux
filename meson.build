project(
  'tvt-iptool-linux',
  'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=0.63.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
  ],
)

gnome = import('gnome')
i18n = import('i18n')
cc = meson.get_compiler('c')

glib_dep = dependency('glib-2.0', version: '>=2.70')
gobject_dep = dependency('gobject-2.0', version: '>=2.70')
gio_dep = dependency('gio-2.0', version: '>=2.70')
gtk_dep = dependency('gtk4', version: '>=4.8')
dl_dep = cc.find_library('dl', required: false)

subdir('src')
subdir('tests')
subdir('data')

summary({
  'Version': meson.project_version(),
  'GTK': gtk_dep.version(),
  'Vendor SDK bundled': false,
}, section: 'TVT IPTool for Linux')
