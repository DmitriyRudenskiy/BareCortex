#!/bin/sh
# uninstall.sh — reverse install.sh via its manifest. Restores backups,
# removes every recorded path, then the prefix (unless backups remain).
# usage: uninstall.sh [--prefix DIR]
set -eu
PREFIX=/opt/nvidia-musl-exp
[ "${1:-}" = --prefix ] && { PREFIX=$2; shift; }
[ $# -eq 0 ] || { echo "usage: uninstall.sh [--prefix DIR]" >&2; exit 64; }
MANIFEST=$PREFIX/manifest.tsv
[ -f "$MANIFEST" ] || { echo "no manifest at $MANIFEST" >&2; exit 1; }

tac() { awk '{a[NR]=$0} END{for(i=NR;i>0;i--) print a[i]}' "$MANIFEST"; } # POSIX reverse

while IFS="$(printf '\t')" read -r kind path extra; do
	case $kind in
	link|file)
		[ -e "$path" ] || [ -L "$path" ] && rm -f "$path"
		# restore a backup if one was taken for this basename
		for b in "$PREFIX/backup/$(basename "$path")."*; do
			[ -e "$b" ] || [ -L "$b" ] || continue
			mv "$b" "$path"
			echo "== restored $path"
		done
		;;
	dir) rmdir "$path" 2>/dev/null || true ;;
	esac
done <<EOF
$(tac)
EOF

if [ -d "$PREFIX/backup" ] && [ -z "$(ls -A "$PREFIX/backup")" ]; then rmdir "$PREFIX/backup"; fi
rm -f "$MANIFEST"
if [ -d "$PREFIX" ] && [ -z "$(ls -A "$PREFIX")" ]; then rmdir "$PREFIX";
else echo "== $PREFIX not empty, left in place"; fi
echo "== uninstalled"
