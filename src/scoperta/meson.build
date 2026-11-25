scoperta_lib = static_library('scoperta', 'scoperta.c', include_directories: inc,
  dependencies: comune_dep)
scoperta_dep = declare_dependency(link_with: scoperta_lib, include_directories: inc,
  dependencies: comune_dep)

test('scoperta', executable('prova_scoperta', 'prova_scoperta.c', dependencies: scoperta_dep))

# Strumento a mano: stampa cio' che trova in 5 secondi (non e' un test).
executable('cerca', 'cerca.c', dependencies: scoperta_dep)

# Stessa cosa senza sanitizer, da eseguire sul tablet. Ricompila i sorgenti
# invece di usare la libreria (che ha il sanitizer) e il comune senza sanitizer.
executable('cerca_senza_sanitizer', ['cerca.c', 'scoperta.c', '../comune/errore.c'],
  include_directories: inc,
  override_options: ['b_sanitize=none', 'b_lundef=true'])

# Il telefono finto prova se stesso (Python): una connessione TLS vera,
# due canali insieme, un comando, un file nei due sensi.
test('telefono_finto', find_program('python3'),
  args: [meson.project_source_root() / 'prove/telefono_finto/prova.py'],
  timeout: 60)
