libm = meson.get_compiler('c').find_library('m', required: false)
finestre_lib = static_library('finestre', 'finestre.c', include_directories: inc,
  dependencies: [comune_dep, libm])
finestre_dep = declare_dependency(link_with: finestre_lib, include_directories: inc,
  dependencies: [comune_dep, libm])

test('finestre', executable('prova_finestre', 'prova_finestre.c', dependencies: finestre_dep))
