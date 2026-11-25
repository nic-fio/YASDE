# Strumento di prova a riga di comando (solo con pezzo=tutto).
# La variante senza sanitizer non c'e': le librerie degli altri pezzi sono
# compilate con il sanitizer e ricompilarne i sorgenti qui vorrebbe dire
# copiarne gli elenchi di file, che non sono miei.
executable('yasde-prova', 'yasde-prova.c',
  dependencies: [comune_dep, scoperta_dep, adb_dep, abbinamento_dep, file_dep, componente_dep])

# Finestra di prova dell'ondata 1: un'app del telefono in una finestra spoglia.
sdl3_dep = dependency('sdl3', required: false)
if sdl3_dep.found() and is_variable('video_dep') and is_variable('audio_dep')
  executable('finestra-prova', 'finestra-prova.c',
    dependencies: [comune_dep, scoperta_dep, adb_dep, file_dep, componente_dep, video_dep,
                   audio_dep, sdl3_dep, dependency('threads')])
endif

# Strumento di misura dei flussi video in parallelo (punto aperto A3): niente SDL.
if is_variable('video_dep')
  executable('misura-flussi', 'misura-flussi.c',
    dependencies: [comune_dep, scoperta_dep, adb_dep, file_dep, componente_dep, video_dep,
                   dependency('threads')])
endif

# Prototipo di misura GTK4 (punto aperto A1): composizione senza copie con DMA-BUF.
gtk4_dep = dependency('gtk4', required: false)
if gtk4_dep.found() and is_variable('video_dep')
  executable('prototipo-gtk', 'prototipo-gtk.c',
    dependencies: [comune_dep, scoperta_dep, adb_dep, file_dep, componente_dep, video_dep,
                   gtk4_dep, dependency('threads'), meson.get_compiler('c').find_library('m')])
endif
