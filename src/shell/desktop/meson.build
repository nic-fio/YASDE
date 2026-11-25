# Pezzo shell-desktop (F-02): il desktop: sfondo, icone, widget, menu. Vedi COMPITO.md.
# Definisce shell_desktop_dep. Si prova con: strumenti/prova.sh shell-desktop
risorse_desktop = gnome.compile_resources('risorse-desktop', 'desktop.gresource.xml',
  c_name: 'ph_risorse_desktop', extra_args: ['--manual-register'])
risorse_desktop_lib = static_library('risorse_desktop', risorse_desktop,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

pixbuf_dep = dependency('gdk-pixbuf-2.0')
shell_desktop_lib = static_library('shell_desktop',
  ['desktop.c', 'logica.c', 'logica_wx.c', 'logica_meteo.c', 'immagine.c', 'sfondo.c', 'icone.c', 'widget.c', 'orologio.c', 'wx.c', 'wx_calendario.c', 'wx_telefono.c', 'wx_controlli.c', 'wx_elenchi.c', 'wx_meteo.c', 'meteo_disegno.c', 'meteo_luogo.c', 'dialoghi.c'],
  objects: risorse_desktop_lib.extract_all_objects(recursive: false),
  include_directories: inc, dependencies: [shell_comune_dep, pixbuf_dep])
shell_desktop_dep = declare_dependency(link_with: shell_desktop_lib, include_directories: inc,
  dependencies: [shell_comune_dep, pixbuf_dep])

# Prova di logica, senza schermo.
test('shell-desktop-logica', executable('prova_desktop', 'prova_desktop.c', dependencies: shell_desktop_dep),
  env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo sopra il backend finto (strumenti/demo-sul-tablet.sh desktop); con --prova
# e' anche la prova con schermo (saltata se nel container non c'e' broadway).
demo_desktop = executable('demo-desktop', ['demo.c', 'demo_hook.c'], dependencies: [shell_desktop_dep, shell_finto_dep])
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
