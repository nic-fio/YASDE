# Pezzo "meteo": previsioni da Open-Meteo via HTTPS (OpenSSL, gia' dipendenza
# del progetto per ADB). Vedi COMPITO.md e meteo.h. Definisce meteo_dep.
# La connessione cifrata e' isolata in tls.c (interfaccia tls.h): oggi e' uno
# scheletro, la scrive chi coordina. Le prove usano prova/tls_finto.c al suo
# posto (stessa interfaccia, in chiaro su 127.0.0.1), quindi la libreria di
# prova e' ricompilata SENZA tls.c.
openssl_meteo_dep = dependency('openssl', version: '>=3.0')
threads_meteo_dep = dependency('threads')
libm_meteo_dep = meson.get_compiler('c').find_library('m', required: false)

meteo_nucleo = ['analisi.c', 'http.c', 'meteo.c']
meteo_lib = static_library('meteo', meteo_nucleo + ['tls.c'], include_directories: inc,
  dependencies: [comune_dep, openssl_meteo_dep, threads_meteo_dep, libm_meteo_dep])
meteo_dep = declare_dependency(link_with: meteo_lib, include_directories: inc,
  dependencies: [comune_dep, openssl_meteo_dep, threads_meteo_dep, libm_meteo_dep])

meteo_prova_lib = static_library('meteo_prova', meteo_nucleo, include_directories: inc,
  dependencies: [comune_dep, threads_meteo_dep, libm_meteo_dep])
meteo_prova_src = ['prova/prova_meteo.c', 'prova/tls_finto.c']
test('meteo',
  executable('prova_meteo', meteo_prova_src,
    include_directories: [inc, include_directories('.')],
    link_with: [meteo_prova_lib],
    dependencies: [comune_dep, threads_meteo_dep, libm_meteo_dep]),
  timeout: 300)

# Prove con thread sotto ThreadSanitizer: sorgenti ricompilati con
# -fsanitize=thread (non si mescola con AddressSanitizer: anche comune).
tsan = ['b_sanitize=thread']
meteo_comune_t = static_library('comune_tsan_meteo', ['../comune/errore.c', '../comune/buffer.c'],
  include_directories: inc, override_options: tsan)
meteo_prova_lib_t = static_library('meteo_prova_tsan', meteo_nucleo, include_directories: inc,
  dependencies: [threads_meteo_dep, libm_meteo_dep], override_options: tsan)
test('meteo-tsan',
  executable('prova_meteo_tsan', meteo_prova_src,
    include_directories: [inc, include_directories('.')],
    link_with: [meteo_prova_lib_t, meteo_comune_t],
    dependencies: [threads_meteo_dep, libm_meteo_dep],
    override_options: tsan),
  args: ['concorrenza'],
  timeout: 600, is_parallel: false)

# Prova della connessione cifrata VERA (tls.c) contro un server OpenSSL locale
# scritto nella prova; certificati generati al momento (nessuna chiave nel
# repository). Collega meteo_lib, che contiene il tls.c vero.
tls_prova_src = ['prova/prova_tls.c']
# tls.c compilato CON -DPH_METEO_PROVE (solo qui esiste ph_tls_prova_ancore).
meteo_tls_prova_lib = static_library('meteo_tls_prova', meteo_nucleo + ['tls.c'], include_directories: inc,
  c_args: ['-DPH_METEO_PROVE'],
  dependencies: [comune_dep, openssl_meteo_dep, threads_meteo_dep, libm_meteo_dep])
test('meteo-tls',
  executable('prova_tls', tls_prova_src,
    include_directories: [inc, include_directories('.')],
    link_with: [meteo_tls_prova_lib],
    dependencies: [comune_dep, openssl_meteo_dep, threads_meteo_dep, libm_meteo_dep]),
  timeout: 300, is_parallel: false)
# Nel programma vero (meteo_lib, senza PH_METEO_PROVE) il simbolo non deve esistere;
# nella libreria delle prove si'. (Se nm mancasse la prova fallirebbe: mai verde a vuoto.)
test('meteo-senza-ancore-di-prova',
  find_program('sh'),
  args: ['-c', 'nm "$2" | grep -q " T ph_tls_prova_ancore" && ! nm "$1" | grep -q ph_tls_prova_ancore', 'sh',
         meteo_lib, meteo_tls_prova_lib])
