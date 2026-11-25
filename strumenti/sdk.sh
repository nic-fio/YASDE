#!/bin/sh
# Scarica l'SDK di Android che serve a compilare il jar del telefono
# (piattaforma 35 e build-tools 35.0.0) nella cartella /android-sdk.
# Si lancia UNA volta, quando si prepara da zero il server di compilazione,
# dentro il container costruito da strumenti/Containerfile:
#
#   POD="podman --root /media/YASDE/containers --runroot /media/YASDE/containers-run --cgroup-manager=cgroupfs"
#   $POD build -t localhost/yasde-sviluppo:schermo -f strumenti/Containerfile strumenti
#   mkdir -p /media/YASDE/android-sdk
#   $POD run --rm -v /media/YASDE/android-sdk:/android-sdk -v "$PWD/strumenti":/s:ro \
#        localhost/yasde-sviluppo:schermo sh /s/sdk.sh
#
# Poi strumenti/prova.sh monta /media/YASDE/android-sdk in sola lettura a ogni
# compilazione. Tutto il resto dell'ambiente (cache del pacchetto, icone di
# terzi) lo riscaricano da soli pacchetto/costruisci.sh e pacchetto/icone-kora.sh.
set -e
cd /android-sdk
curl -fsSL -o ct.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
rm -rf cmdline-tools && unzip -q ct.zip && rm ct.zip
yes | cmdline-tools/bin/sdkmanager --sdk_root=/android-sdk --licenses >/dev/null
cmdline-tools/bin/sdkmanager --sdk_root=/android-sdk "platforms;android-35" "build-tools;35.0.0"
ls /android-sdk/platforms/android-35/android.jar /android-sdk/build-tools/35.0.0/d8
