# Pezzo "dati": lato PC dei dati del telefono (app, icone, stato, notifiche,
# miniature, memoria, ricerca di file, appunti). La libreria usa solo i header
# di componente e adb; i test collegano un telefono finto (prova/finto.c) al
# posto di ph_componente_* e ph_canale_*, cosi' funzionano sia con
# -Dpezzo=dati sia con 'tutto'.
threads_dep = dependency('threads')

dati_sorgenti = ['utf8.c', 'canale.c', 'richieste.c', 'cerca.c', 'appunti.c', 'nuovi.c']

dati_lib = static_library('dati', dati_sorgenti,
  include_directories: inc,
  dependencies: [comune_dep, threads_dep])

dati_dipendenze = [comune_dep, threads_dep]
if is_variable('componente_dep')
  dati_dipendenze += componente_dep
endif
if is_variable('adb_dep')
  dati_dipendenze += adb_dep
endif
dati_dep = declare_dependency(
  link_with: dati_lib,
  include_directories: inc,
  dependencies: dati_dipendenze)

# ph_msg_* e ph_leggi_* vivono nel pezzo componente, che con -Dpezzo=dati non
# c'e': i test ricompilano il solo messaggi.c (parte pura, senza rete). Non
# fa parte di dati_lib, altrimenti con 'tutto' i simboli sarebbero doppi.
prova_inc = [inc, include_directories('prova')]
prova_sorgenti = ['prova/prova_dati.c', 'prova/finto.c']

messaggi_lib = static_library('dati_messaggi', '../componente/messaggi.c',
  include_directories: inc,
  dependencies: [comune_dep])

# Qui NON si collegano adb_dep/componente_dep: i simboli li fornisce il finto.
test('dati-finto',
  executable('prova_dati', prova_sorgenti,
    include_directories: prova_inc,
    link_with: [dati_lib, messaggi_lib],
    dependencies: [comune_dep, threads_dep]),
  timeout: 300)

# Concorrenza (ricerca e appunti) con ThreadSanitizer: sorgenti ricompilati con
# -fsanitize=thread (non si mescola con AddressSanitizer: anche comune).
tsan = ['b_sanitize=thread']
dati_comune_t = static_library('comune_tsan_dati', ['../comune/errore.c', '../comune/buffer.c'],
  include_directories: inc, override_options: tsan)
dati_lib_t = static_library('dati_tsan', dati_sorgenti + ['../componente/messaggi.c'],
  include_directories: inc, dependencies: [threads_dep], override_options: tsan)
test('dati-finto-tsan',
  executable('prova_dati_tsan', prova_sorgenti,
    include_directories: prova_inc,
    link_with: [dati_lib_t, dati_comune_t],
    dependencies: [threads_dep],
    override_options: tsan),
  args: ['concorrenza'],
  timeout: 600, is_parallel: false)
