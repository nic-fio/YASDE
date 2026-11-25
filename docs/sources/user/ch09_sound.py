# Manuale utente di YASDE: capitolo 9.

from build import c, note, p, rif, shot, steps, table, tip, ui, ul, warn


S1 = p("YASDE sends the sound of the phone's apps to the PC speakers. The sound starts about five seconds after the first app window "
       "opens, and stays on while YASDE is connected. If the sound channel drops it is reopened by itself a few times.", lead=True) + ul([
    "What is sent: sound of apps and media, games, alarms and notification sounds.",
    "What stays on the phone: the audio of phone calls and the ringtone.",
    "The sound plays through the PC's normal audio output, in the PipeWire service.",
]) + note("audio and video are kept in step: a video in a browser plays with its sound aligned.", "Synchronized.")

S2 = p("The " + ui("Volume: 100%") + " button on the taskbar opens the volume popup.", lead=True) + \
    shot("volume.png", "«FIG» — The volume popup") + table(["Control", "What it does"], [
        [ui("Volume"), "The slider. Its value is shown on the right (" + ui("Mute") + " or a percentage). Moving it ends the mute."],
        [ui("Mute"), "Silences the sound; the pill shows " + ui("On") + " or " + ui("Off") + "."],
    ], "«TAB» — The volume popup") + \
    p("The popup says " + ui("Adjusts YASDE audio on the PC. The phone volume does not change.") + " The slider is a gain applied on the PC. "
      "It starts at 100% the first time; afterwards YASDE remembers the last level you set, separately for each phone. "
      "The mute is not remembered: YASDE starts with sound on.") + \
    tip("muting does not stop the sound channel: unmuting is immediate.", "Instant.")

S3 = p("To make sure apps start their sound, YASDE raises the phone's media volume to the maximum while it captures sound, "
       "and puts it back when it stops.", lead=True) + ul([
    "If you change the phone's volume yourself in the meantime, YASDE leaves your value alone.",
    "The change is registered for restoration before it is made, so it is undone even if YASDE or the connection fails: at the latest the next time YASDE starts. If the restore cannot be made safe, the volume is not touched.",
    "The popup slider never changes the phone's volume.",
])

CHAPTER = ("Sound", [
    ("How the sound reaches the PC", S1),
    ("The volume control", S2),
    ("What happens to the phone's volume", S3),
])
