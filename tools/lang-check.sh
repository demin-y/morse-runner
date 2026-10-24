#!/bin/sh
# Checks the translations: every text id of the sources is in lang/en.ini,
# en.ini has no stale ids, and the other files only use the ids of en.ini.
cd "$(dirname "$0")/.." || exit 1
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

ids() { sed -n 's/^\([A-Za-z0-9_.]*\)=.*/\1/p' "$1" | grep -v '^\(Name\|Code\)$' | sort -u; }

# tr("id", ...), { "id", "text" } pairs, and the "key." / "keygroup." ids of keys.c
{
    grep -oh 'tr("[a-z_.]*"' src/ui/*.c | sed 's/tr("\(.*\)"/\1/'
    grep -oh '{ *"[a-z_]*\.[a-z_0-9]*" *,' src/ui/*.c | sed 's/{ *"\(.*\)".*/\1/'
    sed -n 's/^ *\[KA_[A-Z_0-9]*\] = { "\([A-Za-z0-9]*\)".*/key.\1/p' src/ui/keys.c
    grep -oh '"keygroup\.[a-z]*"' src/ui/keys.c | tr -d '"'
} | sort -u > "$tmp/sources"
ids lang/en.ini > "$tmp/en"

status=0
missing=$(comm -23 "$tmp/sources" "$tmp/en")
stale=$(comm -13 "$tmp/sources" "$tmp/en")
[ -n "$missing" ] && { echo "lang/en.ini lacks:"; echo "$missing"; status=1; }
[ -n "$stale" ] && { echo "lang/en.ini has ids the sources do not use:"; echo "$stale"; status=1; }
for f in lang/*.ini; do
    [ "$f" = lang/en.ini ] && continue
    ids "$f" > "$tmp/other"
    extra=$(comm -13 "$tmp/en" "$tmp/other")
    [ -n "$extra" ] && { echo "$f has unknown ids:"; echo "$extra"; status=1; }
    echo "$f: $(wc -l < "$tmp/other") of $(wc -l < "$tmp/en") texts translated"
done
exit $status
