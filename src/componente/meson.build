# Pezzo "componente": lato PC del protocollo con il telefono.
# La libreria dipende solo dai header di adb e file; i test collegano un
# telefono finto (prova/finto.c) al posto dell'ADB vero, cosi' funzionano
# sia con -Dpezzo=componente sia con 'tutto'.
threads_dep = dependency('threads')

componente_lib = static_library('componente',
  ['messaggi.c', 'lettura.c', 'componente.c', 'video.c'],
  include_directories: inc,
  dependencies: [comune_dep, threads_dep])

componente_dipendenze = [comune_dep, threads_dep]
if is_variable('adb_dep')
  componente_dipendenze += adb_dep
endif
if is_variable('file_dep')
  componente_dipendenze += file_dep
endif
componente_dep = declare_dependency(
  link_with: componente_lib,
  include_directories: inc,
  dependencies: componente_dipendenze)

test('componente-messaggi',
  executable('prova_componente_messaggi', 'prova/prova_messaggi.c',
    include_directories: inc,
    link_with: componente_lib,
    dependencies: [comune_dep, threads_dep]),
  timeout: 60)

# Il finto sostituisce ph_canale_*, ph_shell_esegui e ph_file_invia: qui NON si
# collega adb_dep/file_dep, altrimenti i simboli sarebbero doppi.
test('componente-telefono-finto',
  executable('prova_componente', ['prova/prova_componente.c', 'prova/finto.c'],
    include_directories: [inc, include_directories('prova')],
    link_with: componente_lib,
    dependencies: [comune_dep, threads_dep]),
  timeout: 120)

# Stessa prova con ThreadSanitizer: sorgenti ricompilati con -fsanitize=thread
# (non si mescola con AddressSanitizer: anche comune).
tsan = ['b_sanitize=thread']
componente_comune_t = static_library('comune_tsan_componente',
  ['../comune/errore.c', '../comune/buffer.c'],
  include_directories: inc, override_options: tsan)
componente_lib_t = static_library('componente_tsan',
  ['messaggi.c', 'lettura.c', 'componente.c', 'video.c'],
  include_directories: inc, dependencies: [threads_dep], override_options: tsan)
test('componente-telefono-finto-tsan',
  executable('prova_componente_tsan', ['prova/prova_componente.c', 'prova/finto.c'],
    include_directories: [inc, include_directories('prova')],
    link_with: [componente_lib_t, componente_comune_t],
    dependencies: [threads_dep],
    override_options: tsan),
  timeout: 600, is_parallel: false)
