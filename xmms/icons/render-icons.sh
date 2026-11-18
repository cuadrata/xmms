#!/bin/sh
# Copyright (C) 2026 The CGTK authors. Under the GNU General Public
# License, version 2 or later: see COPYING.
#
# The icon's PNG files, from its two drawings, with rsvg-convert: the
# headphones up to 24 pixels and the plate from 32. At 16 pixels it is
# xmms_mini.xpm as it is, with ImageMagick. What this makes is kept in
# the repository, so neither is needed to build.
set -e
cd "$(dirname "$0")"

for s in 22 24; do
    rsvg-convert -w $s -h $s xmms-small.svg -o xmms-$s.png
done
for s in 32 48 64 128 256; do
    rsvg-convert -w $s -h $s xmms.svg -o xmms-$s.png
done
convert ../xmms_mini.xpm -strip -define png:exclude-chunks=date,time PNG32:xmms-16.png
