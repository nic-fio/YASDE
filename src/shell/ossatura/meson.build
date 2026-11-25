# Pezzo shell-ossatura (F-01, 4.8, 4.14): applicazione, finestra principale,
# velo, servizi veri. Vedi COMPITO.md. Definisce shell_ossatura_dep e il
# programma "yasde". Si prova con: strumenti/prova.sh shell-ossatura
risorse_ossatura = gnome.compile_resources('risorse-ossatura', 'ossatura.gresource.xml',
  c_name: 'ph_risorse_ossatura', extra_args: ['--manual-register'])
risorse_ossatura_lib = static_library('risorse_ossatura', risorse_ossatura,
  c_args: shell_args_risorse, dependencies: gtk4_dep)

# I pezzi della shell presenti (la superficie arriva con finestra-app).
ossatura_pezzi_dep = []
foreach p : shell_presenti
  if p != 'ossatura' and p != 'superficie'
    ossatura_pezzi_dep += get_variable('shell_' + p.underscorify() + '_dep')
  endif
endforeach

# I servizi veri solo se ci sono tutti i pezzi sotto.
ossatura_sotto_dep = []
ossatura_veri = true
foreach d : ['collegamento_dep', 'dati_dep', 'componente_dep', 'video_dep', 'audio_dep',
             'trasferimenti_dep', 'file_dep', 'adb_dep', 'scoperta_dep', 'abbinamento_dep', 'meteo_dep']
  if is_variable(d)
    ossatura_sotto_dep += get_variable(d)
  else
    ossatura_veri = false
  endif
endforeach
ossatura_args = ['-DPH_OSSATURA_SERVIZI_VERI=' + (ossatura_veri ? '1' : '0')]

shell_ossatura_lib = static_library('shell_ossatura', ['ossatura.c', 'logica.c', 'velo.c', 'finestra.c', 'appunti.c', 'servizi_veri.c',
   'sv_dati.c', 'sv_file.c', 'sv_tr.c', 'sv_flussi.c', 'sv_audio.c', 'sv_extra.c', 'sv_abbina.c', 'sv_meteo.c'],
  objects: risorse_ossatura_lib.extract_all_objects(recursive: false),
  c_args: ossatura_args,
  include_directories: [inc, shell_presenti_inc],
  dependencies: [shell_comune_dep] + ossatura_pezzi_dep + (ossatura_veri ? ossatura_sotto_dep : []))
shell_ossatura_dep = declare_dependency(link_with: shell_ossatura_lib, include_directories: inc,
  dependencies: [shell_comune_dep] + ossatura_pezzi_dep + (ossatura_veri ? ossatura_sotto_dep : []))

executable('yasde', 'main.c', dependencies: shell_ossatura_dep)

# Prova di logica, senza schermo.
test('shell-ossatura-logica', executable('prova_ossatura', 'prova_ossatura.c',
  c_args: ossatura_args, include_directories: shell_presenti_inc, dependencies: shell_ossatura_dep), env: shell_ambiente_logica, suite: 'shell', timeout: 60)

# Demo della shell intera sopra il backend finto; con --prova e' anche la
# prova con schermo (saltata se manca broadway).
# Solo il demo usa la variante di finestra-app con gli aiuti senza mouse (--passi hook:...):
# sta PRIMA, cosi' il linker prende da li' i simboli di finestra-app e il programma vero
# (yasde) resta senza gli aiuti.
demo_ossatura_dep = shell_presenti.contains('finestra-app') ? [shell_finestra_app_demo_dep] : []
demo_ossatura = executable('demo-ossatura', 'demo.c', c_args: ossatura_args,
  include_directories: shell_presenti_inc, dependencies: demo_ossatura_dep + [shell_ossatura_dep, shell_finto_dep])
test('shell-ossatura-schermo', demo_ossatura, args: ['--prova', '3000'],
  env: shell_ambiente_schermo, suite: 'shell', timeout: 60)

# Prova dei servizi veri senza telefono e senza schermo, sopra pezzi sotto
# finti che dormono come la rete (finti_sotto.c): nessuna chiamata blocca il
# thread GTK, prestiti restituiti, chiusura con richieste in corso.
if ossatura_veri
  test('shell-ossatura-servizi-veri', executable('prova_servizi_veri',
    ['prova_servizi_veri.c', 'finti_sotto.c', 'servizi_veri.c', 'sv_dati.c', 'sv_file.c', 'sv_tr.c',
     'sv_flussi.c', 'sv_audio.c', 'sv_extra.c', 'sv_abbina.c', 'sv_meteo.c', 'logica.c'],
    c_args: ossatura_args, include_directories: [inc, shell_presenti_inc],
    dependencies: [shell_comune_dep]), env: shell_ambiente_logica, suite: 'shell', timeout: 120)
endif

# Prova del meteo dei servizi veri (stessi pezzi sotto finti).
if ossatura_veri
  test('shell-ossatura-meteo-veri', executable('prova_meteo_veri',
    ['prova_meteo_veri.c', 'finti_sotto.c', 'servizi_veri.c', 'sv_dati.c', 'sv_file.c', 'sv_tr.c',
     'sv_flussi.c', 'sv_audio.c', 'sv_extra.c', 'sv_abbina.c', 'sv_meteo.c', 'logica.c'],
    c_args: ossatura_args, include_directories: [inc, shell_presenti_inc],
    dependencies: [shell_comune_dep]), env: shell_ambiente_logica, suite: 'shell', timeout: 120)
endif

# Prova dell'avviso «decodifica col processore» (stessi pezzi sotto finti).
if ossatura_veri
  test('shell-ossatura-decodifica-avviso', executable('prova_decodifica_avviso',
    ['prova_decodifica_avviso.c', 'finti_sotto.c', 'servizi_veri.c', 'sv_dati.c', 'sv_file.c', 'sv_tr.c',
     'sv_flussi.c', 'sv_audio.c', 'sv_extra.c', 'sv_abbina.c', 'sv_meteo.c', 'logica.c'],
    c_args: ossatura_args, include_directories: [inc, shell_presenti_inc],
    dependencies: [shell_comune_dep]), env: shell_ambiente_logica, suite: 'shell', timeout: 120)
endif
