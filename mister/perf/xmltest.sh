#!/bin/bash
# xmltest.sh SYSTEMS_TSV: the old (sed, tr) and the new (bash) escaping give the same bytes, for every field of
# systems.tsv and for strings with every special character
oldxml() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'; }
newxml() {
	local s=$1
	s=${s//&/\&amp;}
	s=${s//</\&lt;}
	s=${s//>/\&gt;}
	s=${s//\"/\&quot;}
	REPLY=$s
}
bad=0; n=0
check() {
	local a b
	a=$(echo "$1" | oldxml); newxml "$1"; b=$REPLY
	n=$((n+1)); [ "$a" == "$b" ] || { bad=$((bad+1)); printf 'DIFF [%s]: [%s] vs [%s]\n' "$1" "$a" "$b"; }
	a=$(echo "$1" | tr '[:lower:]' '[:upper:]'); b=${1^^}
	n=$((n+1)); [ "$a" == "$b" ] || { bad=$((bad+1)); printf 'UPPER DIFF [%s]: [%s] vs [%s]\n' "$1" "$a" "$b"; }
}
while IFS=$'\t' read -r name full folder exts rest; do
	case "$name" in '' | '#'*) continue ;; esac
	for f in "$name" "$full" "$folder" "$exts" "/media/fat/games/$folder" "/media/fat/${folder:1}"; do check "$f"; done
done <"$1"
for s in '&' '<' '>' '"' '&amp;' 'a&b<c>d"e' '&&<<>>""' "Tom & Jerry's <\"Best\"> 100%" '\&' '\' '*' '?' '[a]' '$x' '~' ' lead' 'trail ' 'é&ü' '.sfc .SMC .zip'; do check "$s"; done
echo "$n checks, $bad different (bash $BASH_VERSION)"
