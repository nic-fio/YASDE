# La shell (GTK4: compare solo sotto src/shell/). Vedi src/shell/CONTRATTI.md.
# Qui: il pezzo comune (contratti realizzati: consegna, contesto, servizi,
# stile) e il backend finto, sempre; poi i pezzi della shell:
#  - con -Dpezzo=shell-<pezzo> solo quel pezzo;
#  - con pezzo=tutto solo quelli in shell_pronti (meson.build di radice).
gtk4_dep = dependency('gtk4', version: '>= 4.14', required: pezzo != 'tutto')
if not gtk4_dep.found()
  subdir_done()
endif
gnome = import('gnome')
threads_dep = dependency('threads')
libm_dep = meson.get_compiler('c').find_library('m', required: false)

# Ambiente delle prove con schermo (programma demo con --prova): schermo
# virtuale broadway se dentro.sh lo ha avviato (BROADWAY_DISPLAY), altrimenti
# il demo torna 77 = prova saltata. GTK lascia memoria "ancora raggiungibile"
# alla fine: niente controllo delle perdite in queste prove (resta nelle
# prove di logica).
shell_ambiente_schermo = ['GDK_BACKEND=broadway', 'G_DEBUG=fatal-criticals',
                          'ASAN_OPTIONS=detect_leaks=0', 'NO_AT_BRIDGE=1']
# Prove di logica: controllo delle perdite attivo, tranne quelle delle
# librerie di terzi elencate nel file.
shell_ambiente_logica = ['G_DEBUG=fatal-criticals',
  'LSAN_OPTIONS=suppressions=' + meson.current_source_dir() / 'soppressioni-lsan.txt']

# Le risorse (CSS) generate da glib-compile-resources sono una stringa lunga:
# si compilano a parte senza l'avviso sulle stringhe oltre 4095 byte. Ogni
# pezzo fa lo stesso con shell_args_risorse.
shell_args_risorse = ['-Wno-overlength-strings']
risorse_comune = gnome.compile_resources('risorse-comune', 'comune/comune.gresource.xml',
  source_dir: 'comune', c_name: 'ph_risorse_comune', extra_args: ['--manual-register'])
risorse_comune_lib = static_library('risorse_comune', risorse_comune,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

shell_comune_lib = static_library('shell_comune',
  ['comune/consegna.c', 'comune/ascoltatori.c', 'comune/shell.c', 'comune/stile.c',
   'comune/servizi.c', 'comune/vetro.c'],
  objects: risorse_comune_lib.extract_all_objects(recursive: false),
  include_directories: inc,
  dependencies: [gtk4_dep, config_dep, comune_dep, threads_dep, libm_dep])
shell_comune_dep = declare_dependency(link_with: shell_comune_lib, include_directories: inc,
  dependencies: [gtk4_dep, config_dep, comune_dep, threads_dep, libm_dep])

test('shell-comune', executable('prova_shell_comune', 'comune/prova_comune.c',
  dependencies: shell_comune_dep), env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Prova del vetro con GTK vero (broadway; senza schermo esce con 77 = saltata).
test('shell-comune-vetro', executable('prova_vetro', 'comune/prova_vetro.c',
  dependencies: shell_comune_dep), env: shell_ambiente_schermo, suite: 'shell', timeout: 300)

subdir('finto')

# Pezzi presenti in questa compilazione: l'ossatura installa solo quelli.
shell_pezzi = ['superficie', 'finestra-app', 'barra', 'launcher', 'desktop', 'trasferimento',
               'sistema', 'connessione', 'ossatura']
shell_presenti = []
foreach p : shell_pezzi
  if pezzo == 'shell-' + p or (pezzo == 'tutto' and shell_pronti.contains(p))
    shell_presenti += p
  endif
endforeach
presenti_cfg = configuration_data()
foreach p : shell_pezzi
  presenti_cfg.set10('PH_SHELL_HA_' + p.underscorify().to_upper(), shell_presenti.contains(p))
endforeach
configure_file(output: 'pezzi_presenti.h', configuration: presenti_cfg)
shell_presenti_inc = include_directories('.')

foreach p : shell_presenti
  subdir(p)
endforeach
