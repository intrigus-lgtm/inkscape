# SPDX-License-Identifier: GPL-2.0-or-later
#! /bin/bash

tmp="$(mktemp -u)"

files="$(find share/ui \( -name '*.ui' -or -name '*.glade' \))"
for f in $files; do
    gtk4-builder-tool simplify --3to4 "$f" > "$tmp"
    mv "$tmp" "$f"
    perl -0777 -i -pe 's/<requires lib="gtk" version="4.0"\/>\n\s*<requires lib="gtk" version="4.0"\/>\s*\n/<requires lib="gtk" version="4.0"\/>\n/gs' "$f"
    sed -i '/<property name="visible">0<\/property>/d' "$f"
    perl bool-replace.perl "$f"
done
