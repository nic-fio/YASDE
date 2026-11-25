#!/bin/sh
# Scarica l'SDK di Android che serve a compilare il jar del telefono
# (piattaforma 35 e build-tools 35.0.0) nella cartella /android-sdk.
# Non si lancia a mano: lo esegue strumenti/prepara-server.sh dentro il
# container, quando si prepara da zero il server di compilazione. Poi
# strumenti/prova.sh monta /media/YASDE/android-sdk in sola lettura a ogni
# compilazione.
set -e
cd /android-sdk
curl -fsSL -o ct.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
rm -rf cmdline-tools && unzip -q ct.zip && rm ct.zip
yes | cmdline-tools/bin/sdkmanager --sdk_root=/android-sdk --licenses >/dev/null
cmdline-tools/bin/sdkmanager --sdk_root=/android-sdk "platforms;android-35" "build-tools;35.0.0"
ls /android-sdk/platforms/android-35/android.jar /android-sdk/build-tools/35.0.0/d8
