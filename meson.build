project('rendirt', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++11',
    'default_library=static',
    'b_lto=true',
    'b_ndebug=if-release',
    'warning_level=2',
    'werror=true'])

incdir = include_directories('.')

rendirt_sources = ['rendirt.cpp']
rendirt_lib = library('rendirt', rendirt_sources,
  include_directories: incdir,
  install: true)

install_headers('rendirt.hpp')

rendirt_dep = declare_dependency(
  include_directories: incdir,
  link_with: rendirt_lib)

subdir('examples')
