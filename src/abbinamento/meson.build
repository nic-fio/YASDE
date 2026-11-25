openssl_dep = dependency('openssl', version: '>=3.0')
abbinamento_lib = static_library('abbinamento', ['spake2.c', 'abbinamento.c'],
  include_directories: inc, dependencies: [openssl_dep, comune_dep],
  c_args: ['-DOPENSSL_API_COMPAT=10100', '-Wno-deprecated-declarations'])
abbinamento_dep = declare_dependency(link_with: abbinamento_lib, include_directories: inc,
  dependencies: [openssl_dep, comune_dep])
# Il test usa solo la parte SPAKE2 (pura): non ha bisogno del pezzo adb.
test('spake2', executable('prova_spake2', 'prova_spake2.c', dependencies: abbinamento_dep),
  timeout: 120)
