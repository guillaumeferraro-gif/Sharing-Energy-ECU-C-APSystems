#!/bin/sh
# ACC_CACHE_RAPIDE=__SESSION__
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
ROOT=''
SESSION=__SESSION__
COUNT=241
INTERVAL=1
WINDOW=240
RETENTION=900
SELF="$ROOT/home/update_from_app/update_localweb/assist"
OUT="$ROOT/home/local_web/pages/resources/acc_cache_$SESSION"
GRID="$ROOT/tmp/gridQuickPower.conf"
STATUS="$ROOT/tmp/gridStatus.conf"
DETAIL="$ROOT/tmp/meterDataTemp.conf"
FUNCTIONS="$ROOT/etc/yuneng/meterFuncNewInfo.conf"

# Ne retirer que l'assist de cette session, comme dans le collecteur v3.3.
remove_self() {
    if [ "$0" = "$SELF" ] && [ -f "$SELF" ] && [ ! -L "$SELF" ] &&
       grep -Fqx "# ACC_CACHE_RAPIDE=$SESSION" "$SELF"; then
        rm -f "$SELF"
    fi
}
trap remove_self 0
trap 'exit 1' 1 2 15
[ "$(tr -d '\r\n' < "$ROOT/etc/yuneng/version.conf")" = C1.2.27A ] || exit 11
[ "$(head -c 12 "$ROOT/etc/yuneng/ecuid.conf")" = 000000000000 ] || exit 12
mkdir -m 755 "$OUT" || exit 13
(
    trap - 0 1 2 15
    sleep "$RETENTION"
    if [ -d "$OUT" ] && [ ! -L "$OUT" ]; then
        rm -f "$OUT/points.txt" "$OUT/fonction.json" "$OUT/fonction.part"
        rmdir "$OUT" 2>/dev/null || :
    fi
) </dev/null >/dev/null 2>&1 &

# Copie bornée du seul réglage de fonctions, avant la mesure. Le PC ne conserve que FUNC.
if [ ! -L "$FUNCTIONS" ] && [ -f "$FUNCTIONS" ] && [ -r "$FUNCTIONS" ]; then
    size=$(wc -c < "$FUNCTIONS")
    if [ "$size" -gt 0 ] && [ "$size" -le 2047 ]; then
        if head -c 2048 "$FUNCTIONS" > "$OUT/fonction.part"; then
            chmod 644 "$OUT/fonction.part"
            mv "$OUT/fonction.part" "$OUT/fonction.json"
        fi
    fi
fi

# Sur le boîtier la commande stat peut n'exister qu'en tant qu'applet BusyBox.
stat_mode=none
if stat -c %Y "$ROOT/tmp" >/dev/null 2>&1; then
    stat_mode=stat
elif /bin/busybox stat -c %Y "$ROOT/tmp" >/dev/null 2>&1; then
    stat_mode=busybox
fi
mtime() {
    case "$stat_mode" in
        stat) stat -c %Y "$1" 2>/dev/null || printf '?' ;;
        busybox) /bin/busybox stat -c %Y "$1" 2>/dev/null || printf '?' ;;
        *) printf '?' ;;
    esac
}
snapshot() {
    if [ -L "$1" ] || [ ! -f "$1" ] || [ ! -r "$1" ]; then
        printf '?\t?\t?'
        return
    fi
    before=$(mtime "$1")
    value=$(head -c "$2" "$1" 2>/dev/null | tr '\t\r\n' '   ')
    after=$(mtime "$1")
    printf '%s\t%s\t%s' "$before" "$value" "$after"
}
printf 'ACCW2\t%s\t%s\n' "$SESSION" "$COUNT" > "$OUT/points.txt"
chmod 644 "$OUT/points.txt"
read -r started unused < "$ROOT/proc/uptime" || started='?'
started=${started%%.*}
i=1
while [ "$i" -le "$COUNT" ]; do
    grid=$(snapshot "$GRID" 32)
    detail=$(snapshot "$DETAIL" 1024)
    valid='?'
    if [ -f "$STATUS" ] && [ ! -L "$STATUS" ] && [ -r "$STATUS" ]; then
        valid=$(head -c 4 "$STATUS" 2>/dev/null) || valid='?'
        case "$valid" in 0|1) : ;; *) valid='?' ;; esac
    fi
    read -r uptime unused < "$ROOT/proc/uptime" || uptime='?'
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$i" "$(date +%s)" "$uptime" "$grid" "$valid" "$detail" >> "$OUT/points.txt"
    seconds=${uptime%%.*}
    case "$started:$seconds" in
        *[!0-9:]*|:*|*:) : ;;
        *) [ "$((seconds - started))" -lt "$WINDOW" ] || break ;;
    esac
    [ "$i" -lt "$COUNT" ] || break
    sleep "$INTERVAL"
    i=$((i + 1))
done
printf 'FIN\t%s\n' "$i" >> "$OUT/points.txt"
