#!/bin/bash
declare -A FILES=(
 ["Liberation Serif"]=/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf
 ["Liberation Sans"]=/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
 ["Liberation Mono"]=/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf
)
printf "%-18s %4s | %-28s | %s\n" FUENTE PT "coincidencias (variante a)" "variante b"
for face in "Liberation Serif" "Liberation Sans" "Liberation Mono"; do
  for pt in 8 10 12 14 24; do
    WINEDEBUG=-all ./gdi_metrics.exe "$face" $pt 2>/dev/null | grep "^GDI adv" | awk '{print $3, $4}' > g.txt
    QT_QPA_PLATFORM=offscreen ./qt_metrics "${FILES[$face]}" $pt 96 2>/dev/null | grep "^QT  adv" | sed 's/design=[0-9.]*//' | awk '{gsub(/a=|b=/,"");print $3, $4, $5}' > q.txt
    oh=$(WINEDEBUG=-all ./gdi_metrics.exe "$face" $pt 2>/dev/null | grep "^GDI tm" | grep -o "overhang=[0-9-]*")
    res=$(join g.txt q.txt | awk '{if($2==$3)a++; if($2==$4)b++; n++} END {printf "%d/%d  %d/%d", a,n,b,n}')
    printf "%-18s %4s | %-28s | %s\n" "$face" "$pt" "$(echo $res|awk '{print $1}')" "$(echo $res|awk '{print $2}')  $oh"
  done
done
