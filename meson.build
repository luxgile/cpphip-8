project('cpphip8', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3',
                     'cpp_std=c++23'])

sdl3 = dependency('sdl3')

exe = executable('cpphip8', 'main.cpp',
  install : true,
  dependencies: sdl3)

test('basic', exe)
