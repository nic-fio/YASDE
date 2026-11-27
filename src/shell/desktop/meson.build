# Pezzo shell-desktop (F-02): il desktop: sfondo, icone, widget, menu. Vedi COMPITO.md.
# Definisce shell_desktop_dep. Si prova con: strumenti/prova.sh shell-desktop
risorse_desktop = gnome.compile_resources('risorse-desktop', 'desktop.gresource.xml',
  c_name: 'ph_risorse_desktop', extra_args: ['--manual-register'])
risorse_desktop_lib = static_library('risorse_desktop', risorse_desktop,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

pixbuf_dep = dependency('gdk-pixbuf-2.0')
# agente A: inizio
# Logica pura dei widget Memoria, Notifiche, Calendario del giorno e Stato (logica_vecchi.h):
# libreria a parte (collegata a shell_desktop_lib con link_with, sotto), cosi' l'elenco dei sorgenti
# sotto resta com'e'. Le sue prove: prova_vecchi.c (test in fondo al file).
logica_vecchi_lib = static_library('logica_vecchi', ['logica_vecchi.c'],
  include_directories: inc, dependencies: [shell_comune_dep, pixbuf_dep])
# agente A: fine
shell_desktop_lib = static_library('shell_desktop',
  ['desktop.c', 'logica.c', 'logica_wx.c', 'logica_meteo.c', 'logica_comp.c', 'componenti.c', 'immagine.c', 'sfondo.c', 'icone.c', 'widget.c', 'orologio.c', 'wx.c', 'wx_calendario.c', 'wx_telefono.c', 'logica_telefono.c', 'wx_controlli.c', 'wx_elenchi.c', 'wx_meteo.c', 'meteo_disegno.c', 'meteo_luogo.c', 'dialoghi.c', 'logica_controlli.c', 'logica_note.c', 'wx_note.c'],
  objects: risorse_desktop_lib.extract_all_objects(recursive: false),
  link_with: logica_vecchi_lib, # agente A
  include_directories: inc, dependencies: [shell_comune_dep, pixbuf_dep])
shell_desktop_dep = declare_dependency(link_with: shell_desktop_lib, include_directories: inc,
  dependencies: [shell_comune_dep, pixbuf_dep])

# Prova di logica, senza schermo.
test('shell-desktop-logica', executable('prova_desktop', 'prova_desktop.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh desktop); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_desktop = executable('demo-desktop', ['demo.c', 'demo_hook.c', 'demo_controlli.c', 'demo_meteo.c', 'demo_note.c', 'demo_senzafondo.c'], dependencies: [shell_desktop_dep, shell_finto_dep])
test('shell-desktop-schermo', demo_desktop, args: ['--prova', '6000', '--mostra',
  'wx:meteo:0@500,snap-profilo@1300,finto:meteo-nuovi@1500,finto:meteo-nuovi@1900,verifica-profilo@1600'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Prova del vetro (fase 5): widget del desktop, menu e dialoghi hanno la classe ph-vetro-chiaro.
test('shell-desktop-vetro', demo_desktop, args: ['--prova', '6000', '--mostra',
  'vetro-attendi@10,widget3@300,verifica-vetro@900'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Tessere delle icone vere: niente ombra quadrata (classe ph-desktop-tessera-vera), anche a schermo.
test('shell-desktop-tessere', demo_desktop, args: ['--prova', '6000', '--mostra', 'tessere@300'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Prova d'insieme: ogni widget in ogni stile e misura, gli eventi del telefono finto, i cambi a caldo,
# le rimozioni, i menu e i dialoghi, il cambio di profilo e la chiusura con tutto vivo (sotto ASan).
# La sequenza si chiude da sola; 25 s e' solo il limite di sicurezza.
test('shell-desktop-insieme', demo_desktop, args: ['--prova', '25000', '--mostra', 'stress@300'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Galleria dei componenti di disegno (anello, fette, curva, tratto, riga, numero): disegnati con
# la misura data, tinte che arrivano dal CSS, clic delle righe.
test('shell-desktop-componenti', demo_desktop, args: ['--prova', '6000', '--mostra',
  'componenti@300,componenti-verifica@1500'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Widget predefiniti di un profilo nuovo: a 12 px l'uno dall'altro a schermo, profilo riscritto
# solo alla prima modifica dell'utente.
test('shell-desktop-predefiniti', demo_desktop, args: ['--prova', '6000', '--mostra',
  'predefiniti-attendi@10,predefiniti-verifica@1200'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# agente C: inizio — prova dello stile Adesso del meteo (soglie sulla misura vera)
test('shell-desktop-meteo-wx', executable('prova_meteo_wx', 'prova_meteo_wx.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)
# agente C: fine

# agente D: Interruttori rapidi (capsula con tre tasti tondi). Logica pura, senza schermo.
test('shell-desktop-controlli-logica', executable('prova_controlli', 'prova_controlli.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)
# agente D: disposizione vera dei tasti a schermo, anche con la misura cambiata a caldo (come la maniglia).
test('shell-desktop-controlli-schermo', demo_desktop, args: ['--prova', '14000', '--mostra',
  'controlli-attendi@10,controlli-misure@300,controlli-verifica@900'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
# fine agente D
# agente A: inizio
# Memoria, Notifiche, Calendario del giorno e Stato: che cosa si mostra per ogni misura (logica pura,
# senza schermo), e a schermo righe, clic e i ritocchi di componenti.c (minimo della riga, layout manager).
test('shell-desktop-vecchi-logica', executable('prova_vecchi', 'prova_vecchi.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)
test('shell-desktop-vecchi', demo_desktop, args: ['--prova', '16000', '--mostra', 'vecchi@300'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
# agente A: fine
# --- agente B (passo 2b): logica di batteria in dettaglio e prossima sveglia ---
test('shell-desktop-telefono', executable('prova_telefono', 'prova_telefono.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)
# --- fine agente B ---
# agente C: inizio — prova a schermo del Meteo (stile Adesso): attribuzione dentro e in fondo, soglie a caldo, gestori
test('shell-desktop-meteo-schermo', demo_desktop, args: ['--prova', '45000', '--mostra',
  'meteoa-attendi@10,meteoa-misure@300,meteoa-verifica@900'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 90)
# agente C: fine

# Note rapide: filtro e taglio del testo (logica pura, senza schermo) e prova a schermo: scrivere,
# salvataggio differito e sul fuoco perso, due widget sullo stesso testo, limite incollando,
# rimozione con testo non ancora salvato, cambio di profilo.
test('shell-desktop-note-logica', executable('prova_note', 'prova_note.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)
test('shell-desktop-note-schermo', demo_desktop, args: ['--prova', '10000', '--mostra',
  'note-attendi@10,note-avvia@300'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Stile «Senza fondo» di orologio e meteo (logica: in prova_desktop.c): a schermo niente vetro ne' fondo,
# testo bianco, attribuzione del meteo, maniglia, menu, vetro che torna cambiando stile, misura tenuta.
test('shell-desktop-senzafondo', demo_desktop, args: ['--prova', '10000', '--mostra',
  'senzafondo-attendi@10,senzafondo-avvia@300'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)
