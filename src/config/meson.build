# Persistenza e lingua. Dipende solo da `comune`.
# La tabella delle traduzioni inglesi si genera durante la compilazione dai
# file data/en/*.txt (elenco letto alla configurazione: un file nuovo richiede
# di rilanciare meson setup/reconfigure).
config_base_src = ['toml.c', 'percorsi.c', 'lingua_testo.c']

# Generatore: usa il lettore e la verifica dei segnaposto, senza la tabella.
genera = executable('genera_traduzioni', ['genera_traduzioni.c', config_base_src],
  include_directories: inc, dependencies: comune_dep)

en_elenco = run_command(find_program('python3'), '-I', '-c',
  'import glob,sys; import os; print("\\n".join(sorted(os.path.basename(x) for x in glob.glob(sys.argv[1] + "/*.txt"))))',
  meson.project_source_root() / 'data/en', check: true).stdout().strip().split('\n')
en_file = []
foreach f : en_elenco
  if f != ''
    en_file += files('../../data/en' / f)
  endif
endforeach
traduzioni_en = custom_target('traduzioni_en', input: en_file, output: 'traduzioni_en.c',
  command: [genera, '@OUTPUT@', '@INPUT@'])

config_lib = static_library('config',
  [config_base_src, 'config.c', 'profilo.c', 'lingua.c', traduzioni_en],
  include_directories: inc, dependencies: comune_dep)
config_dep = declare_dependency(link_with: config_lib, include_directories: inc,
  dependencies: comune_dep)

test('config', executable('prova_config', 'prova_config.c', dependencies: config_dep))
test('profilo', executable('prova_profilo', 'prova_profilo.c', dependencies: config_dep))
test('lingua_funzioni', executable('prova_lingua_funzioni',
  ['prova_lingua_funzioni.c', 'lingua.c', 'lingua_testo.c', 'tabella_finta.c'],
  include_directories: inc, dependencies: comune_dep))
test('lingua', executable('prova_lingua', 'prova_lingua.c', dependencies: config_dep),
  args: [meson.project_source_root() / 'src', meson.current_source_dir() / 'finti'])

# Il generatore deve far fallire la compilazione con file sbagliati (should_fail)
# e riuscire con uno giusto (controllo che i fallimenti dipendano dal contenuto).
finti = meson.current_source_dir() / 'finti'
test('generatore_ok', genera, args: ['/dev/null', finti / 'gen_ok.txt'])
test('generatore_doppio_tra_file', genera,
  args: ['/dev/null', finti / 'gen_ok.txt', finti / 'gen_doppio.txt'], should_fail: true)
test('generatore_doppio_nel_file', genera, args: ['/dev/null', finti / 'gen_doppio_stesso_file.txt'],
  should_fail: true)
test('generatore_traduzione_vuota', genera, args: ['/dev/null', finti / 'gen_vuota.txt'], should_fail: true)
test('generatore_segnaposto_diversi', genera, args: ['/dev/null', finti / 'gen_segnaposto.txt'],
  should_fail: true)
