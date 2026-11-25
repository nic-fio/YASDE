# Pezzo "trasferimenti": coda dei trasferimenti PC <-> telefono (senza grafica).
# La libreria dipende solo dai header di adb e file; i test collegano un
# telefono finto (prova/finto.c) al posto dell'ADB e del pezzo file veri, cosi'
# funzionano sia con -Dpezzo=trasferimenti sia con 'tutto'.
threads_dep = dependency('threads')

trasferimenti_lib = static_library('trasferimenti', ['trasferimenti.c', 'installa.c'],
  include_directories: inc,
  dependencies: [comune_dep, threads_dep])

trasferimenti_dipendenze = [comune_dep, threads_dep]
if is_variable('adb_dep')
  trasferimenti_dipendenze += adb_dep
endif
if is_variable('file_dep')
  trasferimenti_dipendenze += file_dep
endif
if is_variable('config_dep')
  trasferimenti_dipendenze += config_dep
endif
trasferimenti_dep = declare_dependency(
  link_with: trasferimenti_lib,
  include_directories: inc,
  dependencies: trasferimenti_dipendenze)

# Il finto sostituisce ph_file_* e ph_shell_esegui, ph_adb_viva: qui NON si
# collega adb_dep/file_dep, altrimenti i simboli sarebbero doppi.
prova_sorgenti = ['prova/prova_trasferimenti.c', 'prova/finto.c']
prova_inc = [inc, include_directories('prova')]

test('trasferimenti-finto',
  executable('prova_trasferimenti', prova_sorgenti,
    include_directories: prova_inc,
    link_with: trasferimenti_lib,
    dependencies: [comune_dep, threads_dep]),
  timeout: 300)

# Stessa prova con ThreadSanitizer: sorgenti ricompilati con -fsanitize=thread
# (AddressSanitizer e ThreadSanitizer non si mescolano, anche comune si ricompila).
tr_tsan = ['b_sanitize=thread']
tr_comune_t = static_library('comune_tsan_tr', ['../comune/errore.c', '../comune/buffer.c'],
  include_directories: inc, override_options: tr_tsan)
tr_lib_t = static_library('trasferimenti_tsan', 'trasferimenti.c',
  include_directories: inc, dependencies: [threads_dep], override_options: tr_tsan)
test('trasferimenti-finto-tsan',
  executable('prova_trasferimenti_tsan', prova_sorgenti,
    include_directories: prova_inc,
    link_with: [tr_lib_t, tr_comune_t],
    dependencies: [threads_dep],
    override_options: tr_tsan),
  timeout: 600, is_parallel: false)

# Installazione di APK: stesso schema, con un telefono finto proprio
# (prova/finto_installa.c: ph_file_invia, ph_shell_esegui, ph_t).
# lingua: i sorgenti veri di config con la loro tabella finta (come fa il test
# lingua_funzioni di config), cosi' ph_t e ph_lingua_componi sono quelli veri.
installa_sorgenti = ['prova/prova_installa.c', 'prova/finto_installa.c',
  '../config/lingua.c', '../config/lingua_testo.c', '../config/tabella_finta.c']
test('trasferimenti-installa',
  executable('prova_installa', installa_sorgenti,
    include_directories: prova_inc,
    link_with: trasferimenti_lib,
    dependencies: [comune_dep, threads_dep]),
  timeout: 120)
tr_lib_inst_t = static_library('trasferimenti_installa_tsan', 'installa.c',
  include_directories: inc, dependencies: [threads_dep], override_options: tr_tsan)
test('trasferimenti-installa-tsan',
  executable('prova_installa_tsan', installa_sorgenti,
    include_directories: prova_inc,
    link_with: [tr_lib_inst_t, tr_comune_t],
    dependencies: [threads_dep],
    override_options: tr_tsan),
  timeout: 300, is_parallel: false)
