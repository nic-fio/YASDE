avcodec_dep = dependency('libavcodec')
avutil_dep = dependency('libavutil')
avformat_dep = dependency('libavformat')
swscale_dep = dependency('libswscale')
vulkan_dep = dependency('vulkan')
threads_dep = dependency('threads')
video_lib = static_library('video', ['decodifica.c', 'cattura_nome.c', 'cattura_png.c', 'cattura_mp4.c', 'esporta.c', 'colore.c'],
  include_directories: inc,
  dependencies: [avcodec_dep, avutil_dep, avformat_dep, swscale_dep, vulkan_dep, comune_dep, threads_dep])
video_dep = declare_dependency(link_with: video_lib, include_directories: inc,
  dependencies: [avcodec_dep, avutil_dep, avformat_dep, swscale_dep, vulkan_dep, comune_dep, threads_dep])

misura = executable('misura_decodifica', 'misura_decodifica.c', dependencies: video_dep)

# Prova sul server (senza scheda grafica): un filmato di prova generato da
# FFmpeg, decodificato con la via software. Le vie accelerate si misurano
# sul tablet con lo stesso strumento.
ffmpeg = find_program('ffmpeg', required: false)
if ffmpeg.found()
  campione = custom_target('campione_h264', output: 'campione.h264',
    command: [ffmpeg, '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
              'testsrc2=size=1280x720:rate=60', '-frames:v', '120', '-c:v', 'libx264',
              '-profile:v', 'high', '-bf', '0', '-g', '60', '-pix_fmt', 'yuv420p', '@OUTPUT@'])
  test('decodifica_software', misura, args: [campione, 'software'], timeout: 120)
endif

# Cattura (screenshot e registrazione). I file MP4 prodotti si riaprono con
# libavformat nei test e, dove c'e', con ffprobe.
matematica_dep = meson.get_compiler('c').find_library('m')
prova_png = executable('prova_cattura_png', 'prova/prova_cattura_png.c', dependencies: [video_dep, matematica_dep])
test('cattura_png', prova_png, timeout: 60)

ffprobe = find_program('ffprobe', required: false)
if ffmpeg.found() and ffprobe.found()
  prova_mp4 = executable('prova_cattura_mp4', 'prova/prova_cattura_mp4.c', dependencies: [video_dep, matematica_dep])
  test('cattura_mp4', prova_mp4, args: [campione, ffprobe.full_path()], timeout: 120)
endif

# I SPS/PPS veri per i pacchetti di prova vengono dal campione H.264.
if ffmpeg.found()
  prova_ostili = executable('prova_cattura_ostili', 'prova/prova_cattura_ostili.c', dependencies: video_dep)
  test('cattura_ostili', prova_ostili, args: [campione], timeout: 180)
endif

prova_esporta = executable('prova_esporta', 'prova/prova_esporta.c', dependencies: video_dep)
test('esporta', prova_esporta, timeout: 60)

prova_ordine_via = executable('prova_ordine_via', 'prova/prova_ordine_via.c', dependencies: video_dep)
test('ordine_via', prova_ordine_via, timeout: 30)
