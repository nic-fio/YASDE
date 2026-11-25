# Pezzo "audio": riceve i pacchetti Opus del telefono, li decodifica e li
# riproduce con PipeWire. Dipende solo dai header di adb (ph_canale_*): i test
# usano un canale finto (prova/finto_canale.c) e un'uscita finta, quindi non
# serve ne' il telefono ne' una scheda audio.
threads_dep = dependency('threads')
opus_dep = dependency('opus')
pipewire_dep = dependency('libpipewire-0.3')
math_dep = meson.get_compiler('c').find_library('m')

audio_sorgenti = ['pacchetti.c', 'decodifica.c', 'flusso.c', 'coda.c', 'audio.c']

# I header di PipeWire usano le espressioni a blocco del GNU C, che con
# warning_level=3 (pedantic) diventano errori: solo questo file ha il pedantic spento.
uscita_pw_lib = static_library('audio_uscita_pw', 'uscita_pipewire.c',
  include_directories: inc,
  c_args: ['-Wno-pedantic'],
  dependencies: [comune_dep, threads_dep, pipewire_dep])

audio_lib = static_library('audio', audio_sorgenti,
  include_directories: inc,
  objects: uscita_pw_lib.extract_all_objects(recursive: false),
  dependencies: [comune_dep, threads_dep, opus_dep, pipewire_dep])

audio_dipendenze = [comune_dep, threads_dep, opus_dep, pipewire_dep]
if is_variable('adb_dep')
  audio_dipendenze += adb_dep
endif
audio_dep = declare_dependency(
  link_with: audio_lib,
  include_directories: inc,
  dependencies: audio_dipendenze)

# Qui NON si collega adb_dep: ph_canale_leggi/chiudi li fornisce il finto,
# altrimenti i simboli sarebbero doppi.
prova_inc = [inc, include_directories('prova')]
prova_dip = [comune_dep, threads_dep, opus_dep, math_dep]
foreach t : ['pacchetti', 'decodifica', 'coda', 'audio']
  test('audio-' + t,
    executable('prova_audio_' + t, ['prova/prova_' + t + '.c', 'prova/finto_canale.c'],
      include_directories: prova_inc,
      link_with: audio_lib,
      dependencies: prova_dip + [pipewire_dep]),
    timeout: 120)
endforeach

# Strumento a mano: suona davvero con PipeWire (non e' un test).
executable('suona', ['suona.c', 'prova/finto_canale.c'],
  include_directories: prova_inc,
  link_with: audio_lib,
  dependencies: prova_dip + [pipewire_dep])

# Stessa cosa senza sanitizer, da eseguire sul tablet. Ricompila i sorgenti
# invece di usare la libreria (che ha il sanitizer) e il comune senza sanitizer.
uscita_pw_senza = static_library('audio_uscita_pw_senza', 'uscita_pipewire.c',
  include_directories: inc,
  c_args: ['-Wno-pedantic'],
  dependencies: [threads_dep, pipewire_dep],
  override_options: ['b_sanitize=none', 'b_lundef=true'])
executable('suona_senza_sanitizer',
  ['suona.c', 'prova/finto_canale.c'] + audio_sorgenti + ['../comune/errore.c'],
  include_directories: prova_inc,
  objects: uscita_pw_senza.extract_all_objects(recursive: false),
  dependencies: prova_dip + [pipewire_dep],
  override_options: ['b_sanitize=none', 'b_lundef=true'])
