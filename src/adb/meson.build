# Pezzo "adb": client ADB (connessione cifrata, canali, comandi shell).
openssl_dep = dependency('openssl', version: '>=3.0')
thread_dep = dependency('threads')

adb_lib = static_library('adb',
  ['pacchetto.c', 'chiave.c', 'connessione.c', 'shell.c'],
  include_directories: inc,
  dependencies: [comune_dep, openssl_dep, thread_dep])

adb_dep = declare_dependency(link_with: adb_lib, include_directories: inc,
  dependencies: [comune_dep, openssl_dep, thread_dep])

# Prove: i test usano anche interno.h (stessa cartella) e il finto telefono.
finto = static_library('finto_telefono_adb', 'finto_telefono.c',
  include_directories: inc, dependencies: [adb_dep])
finto_dep = declare_dependency(link_with: finto, dependencies: [adb_dep])

test('adb_pacchetti', executable('prova_pacchetti', 'prova_pacchetti.c', dependencies: adb_dep))
test('adb_chiave', executable('prova_chiave', 'prova_chiave.c', dependencies: adb_dep))
test('adb_canali', executable('prova_canali', 'prova_canali.c', dependencies: finto_dep),
  timeout: 120)

test('adb_velocita', executable('prova_velocita', 'prova_velocita.c', dependencies: finto_dep),
  timeout: 300, is_parallel: false)

# Stesso test dei canali con ThreadSanitizer: libreria, finto telefono e test
# ricompilati con -fsanitize=thread (le sorgenti sono le stesse).
adb_src = ['pacchetto.c', 'chiave.c', 'connessione.c', 'shell.c']
tsan = ['b_sanitize=thread']
# ThreadSanitizer e AddressSanitizer non si mescolano: anche i sorgenti di
# comune (senza modificarli) si ricompilano qui con TSan.
dip_t = [openssl_dep, thread_dep]
comune_t = static_library('comune_tsan', ['../comune/errore.c', '../comune/buffer.c'],
  include_directories: inc, override_options: tsan)
adb_lib_t = static_library('adb_tsan', adb_src, include_directories: inc,
  dependencies: dip_t, override_options: tsan)
finto_t = static_library('finto_telefono_adb_tsan', 'finto_telefono.c', include_directories: inc,
  dependencies: dip_t, override_options: tsan)
test('adb_canali_tsan', executable('prova_canali_tsan', 'prova_canali.c',
  include_directories: inc, link_with: [finto_t, adb_lib_t, comune_t],
  dependencies: dip_t, override_options: tsan),
  timeout: 300, is_parallel: false)

# Strumento a mano (non e' un test): si collega a un telefono vero e lancia un comando.
executable('prova_vera', 'prova_vera.c', dependencies: adb_dep)
