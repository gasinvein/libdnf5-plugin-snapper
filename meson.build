project(
  'libdnf5-plugin-snapper', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=c++20']
)

project_version_list = meson.project_version().split('.')

libdnf5_dep = dependency('libdnf5')
snapper_lib = meson.get_compiler('cpp').find_library('snapper')
snapper_inc = meson.get_compiler('cpp').check_header('snapper/Snapper.h', required: true)

conf_data = configuration_data()
conf_data.set('VERSION_MAJOR', project_version_list[0])
conf_data.set('VERSION_MINOR', project_version_list[1])
conf_data.set('VERSION_MICRO', project_version_list[2])
configure_file(output: 'config.hpp', configuration: conf_data)

configure_file(
  input: 'config/snapper.conf.in',
  output: 'snapper.conf',
  configuration: {},
  install_dir: join_paths(get_option('sysconfdir'), 'dnf', 'libdnf5-plugins'),
  install: true
)

shared_library(
  'snapper',
  'src/snapper.cpp',
  name_prefix: '',
  dependencies : [
    libdnf5_dep,
    snapper_lib
  ],
  install_dir: join_paths(get_option('libdir'), 'libdnf5', 'plugins'),
  install : true
)
