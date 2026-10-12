Zen X-Fi album-art themes (320x240) - 8 themes
================================================
terminal_green, amber, win95_classic, win95_setup, ps1_card, ps1_boot, ps2_towers, ps2_void

Install: copy the contents of out/ (the .rockbox folder) onto the SD card's
.rockbox folder, merge when asked. Then Settings > Theme Settings > Browse Theme Files.
No firmware change. Fonts come from the normal Rockbox install (.rockbox/fonts).

Album art: art is 120x120 on the left of Now Playing. Title line = song title tag,
or the file name (without extension) when there is no title tag.
Rockbox looks for embedded art and for cover.jpg / folder.jpg in the album folder
(Settings > Playback > Album Art: "prefer embedded" is the default; "prefer image file" also works).
Use baseline (not progressive) JPEGs, ideally 200 px or smaller.
No art found -> a "NO ART" placeholder is shown.

Rebuild: python3 gen_themes.py   (needs Pillow; regenerates out/)
Checked with Rockbox's own checkwps tool built for creativezenxfi (all 8 parse OK).
Not yet seen on the player.
