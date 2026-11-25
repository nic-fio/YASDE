# Pezzo "collegamento": la macchina a stati che tiene in vita il rapporto con
# il telefono. La libreria dipende solo dai header dei pezzi sotto; i test
# collegano un mondo finto (prova/finto.c) al posto di scoperta, adb,
# abbinamento, componente, dati e config, cosi' funzionano sia con
# -Dpezzo=collegamento sia con 'tutto'.
threads_dep = dependency('threads')

collegamento_lib = static_library('collegamento', 'collegamento.c',
  include_directories: inc,
  dependencies: [comune_dep, threads_dep])

collegamento_dipendenze = [comune_dep, threads_dep]
foreach d : ['adb_dep', 'scoperta_dep', 'abbinamento_dep', 'componente_dep', 'dati_dep', 'config_dep']
  if is_variable(d)
    collegamento_dipendenze += get_variable(d)
  endif
endforeach
collegamento_dep = declare_dependency(
  link_with: collegamento_lib,
  include_directories: inc,
  dependencies: collegamento_dipendenze)

# Il finto sostituisce tutte le funzioni dei pezzi sotto: qui NON si collega
# nessuna loro dipendenza, altrimenti i simboli sarebbero doppi.
prova_sorgenti = ['prova/prova_collegamento.c', 'prova/finto.c', 'prova/lingua.c']
prova_inc = [inc, include_directories('prova')]

test('collegamento-finto',
  executable('prova_collegamento', prova_sorgenti,
    include_directories: prova_inc,
    link_with: collegamento_lib,
    dependencies: [comune_dep, threads_dep]),
  timeout: 300)

# Stessa prova con ThreadSanitizer: sorgenti ricompilati con -fsanitize=thread
# (AddressSanitizer e ThreadSanitizer non si mescolano, anche comune si ricompila).
col_tsan = ['b_sanitize=thread']
col_comune_t = static_library('comune_tsan_col', ['../comune/errore.c', '../comune/buffer.c'],
  include_directories: inc, override_options: col_tsan)
col_lib_t = static_library('collegamento_tsan', 'collegamento.c',
  include_directories: inc, dependencies: [threads_dep], override_options: col_tsan)
test('collegamento-finto-tsan',
  executable('prova_collegamento_tsan', prova_sorgenti,
    include_directories: prova_inc,
    link_with: [col_lib_t, col_comune_t],
    dependencies: [threads_dep],
    override_options: col_tsan),
  timeout: 600, is_parallel: false)
