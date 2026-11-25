# Copia di file ed elenco cartelle (sync: di ADB). Si compila solo con gli
# include: i simboli ph_canale_* li mette chi collega (adb_dep nell'app, il
# telefono finto nei test).
file_deps = [comune_dep]
if is_variable('adb_dep')
  file_deps += adb_dep
endif
file_lib = static_library('file', ['file.c'], include_directories: inc, dependencies: comune_dep)
file_dep = declare_dependency(link_with: file_lib, include_directories: inc, dependencies: file_deps)

# Per i test: stessa sorgente con attese brevi (la vera e' di 30 s) e il
# telefono finto al posto di adb. Mai insieme a adb_dep: simboli doppi.
file_lib_prova = static_library('file_prova', ['file.c'], include_directories: inc,
  dependencies: comune_dep,
  c_args: ['-DPH_FILE_ATTESA_MS=300', '-DPH_FILE_FETTA_MS=100', '-DPH_FILE_PROVA_GANCI'])
file_lib_corta = static_library('file_corta', ['file.c'], include_directories: inc,
  dependencies: comune_dep,
  c_args: ['-DPH_FILE_ATTESA_MS=300', '-DPH_FILE_FETTA_MS=100', '-DPH_FILE_PROVA_LETTURE_CORTE', '-DPH_FILE_PROVA_GANCI'])
prova_file_corte = executable('prova_file_corte', ['prova_file.c', 'finto_canale.c'],
  include_directories: inc, link_with: file_lib_corta, dependencies: comune_dep)
test('file-letture-corte', prova_file_corte, args: ['corte'], timeout: 180)
prova_file = executable('prova_file', ['prova_file.c', 'finto_canale.c'],
  include_directories: inc, link_with: file_lib_prova, dependencies: comune_dep)
test('file', prova_file, timeout: 180)

# Strumento per provare con un telefono vero: solo con adb (prova.sh tutto).
if is_variable('adb_dep')
  executable('prova_vera', 'prova_vera.c', dependencies: file_dep)
endif
