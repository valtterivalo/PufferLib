#!/bin/bash
set -euo pipefail
cache=$1
out=$2
root=$(cd "$(dirname "$0")/../../.." && pwd)
bin=$(mktemp -d)/osrs_export
cc -std=c11 -O2 -fwrapv -D_DEFAULT_SOURCE -I"$root" -o "$bin" "$root/ocean/osrs/tools/osrs_export.c" -lz -lbz2 -lm
mkdir -p "$out"
x() { "$bin" "$cache" "$out" "$@"; }

x scene zulrah 35,47 35,48
x scene inferno 35,83 altobjects=inferno_zuk:30327,30328,30329,30330,30332,30333,30334,30336,30337,30338
x scene colosseum 28,48
x scene wilderness 47,55 48,55
x collision wilderness 44,48 56,62

x npcs zulrah npc=2042:5068 npc=2043:5806 npc=2044:5068 npc=2045:140 npc=2046:185 \
  gfx=1044 gfx=1045 gfx=1046 gfx=1047 gfx=1230 loc=11700 \
  seq=5069 seq=5071 seq=5072 seq=5073 seq=5804 seq=5807 seq=5808 seq=138

x npcs inferno npc=7691:7574 npc=7692:7578 npc=7693:7581 npc=7694 npc=7695 npc=7696 \
  npc=7697:7597 npc=7698:7605 npc=7699:7610 npc=7700:7593 npc=7701 npc=7706:7566 npc=7707 npc=7708 \
  gfx=157 gfx=448 gfx=449 gfx=450 gfx=451 gfx=659 gfx=660 gfx=1120 gfx=1374 gfx=1375 gfx=1376 gfx=1377 \
  gfx=1378 gfx=1379 gfx=1380 gfx=1381 gfx=1382 gfx=1383 gfx=1384 gfx=1385 \
  loc=30284 loc=30285 loc=30286 loc=30287 \
  seq=7575 seq=7576 seq=7579 seq=7580 seq=7582 seq=7583 seq=7584 seq=7585 seq=7598 seq=7599 seq=7600 \
  seq=7601 seq=7604 seq=7606 seq=7607 seq=7611 seq=7612 seq=7613 seq=7590 seq=7591 seq=7592 seq=7594 \
  seq=7562 seq=7563 seq=7565 seq=7568 seq=7569

x npcs colosseum npc=10880:11101:11595 npc=12810:10847:10848 npc=12811:10859:10860 npc=12812:10843:10845 \
  npc=12813:10843:10845 npc=12814:10850:10851 npc=12815:10853:10854 npc=12816:10856:10857 \
  npc=12817:10892:10894:10893 npc=12818:10869:10866:10868 npc=12819:10903:10895 \
  npc=12821:10883:10888:10882:10884:10885:10886:10887 npc=12823:10823 npc=12824 npc=12825:10828 \
  npc=12826:-1:10817 npc=12834:-1:10872 npc=12835:-1:10872 npc=12836:-1:10872 seqmodel=10896:52586 \
  seq=693 seq=7856 seq=7857 seq=10327 seq=10328 seq=10329 seq=10330 seq=10803 seq=10804 seq=10805 \
  seq=10806 seq=10807 seq=10808 seq=10809 seq=10810 seq=10811 seq=10812 seq=10900 seq=10901

x textures
x sprites
x interfaces
x fonts
x projectiles
x equipment
x icons
