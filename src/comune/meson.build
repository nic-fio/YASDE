comune_lib = static_library('comune', ['errore.c', 'buffer.c'], include_directories: inc)
comune_dep = declare_dependency(link_with: comune_lib, include_directories: inc)
test('comune', executable('prova_comune', 'prova_comune.c', dependencies: comune_dep))
