#!/bin/sh
# ACC_ECU_COPY_TOKEN=00000000000000000000000000000000
# V3.3: snapshot selected code on /home and serialize only named regular files.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
LC_ALL=C
export LC_ALL
umask 077
COPY_TOKEN='00000000000000000000000000000000'
SOURCE_ROOT=''
RETENTION='900'
WEB_BASE="$SOURCE_ROOT/home/local_web/pages/resources"
WEB_DIR="$WEB_BASE/acc_ecu_copy_$COPY_TOKEN"
WORK_BASE="$SOURCE_ROOT/home/update_from_app"
WORK_DIR="$WORK_BASE/acc_ecu_copy_$COPY_TOKEN"
SELF_PATH="$WORK_BASE/update_localweb/assist"
CONTROL="$WORK_DIR/control"
TAB=$(printf '\t')
PACKED_FILES=0
PACK_TOTAL=0
PHASE_DONE=0
TRANSPORT=pending
COPY_STATE=FAILED
COPY_REASON=unexpected_error
OWN_WORK=0
TOTAL_BYTES=0
SNAPSHOT_BYTES=0
FILE_COUNT=0
MAX_BYTES=268435456
MAX_FILE_BYTES=67108864
MAX_FILES=12000
FREE_TMP=unknown
FREE_WEB=unknown
FREE_WORK=unknown
REQUIRED_WEB=unknown
REQUIRED_WORK=40960
SOURCE_CHECK=pending
WALK_ENTRIES=0
WALK_FILES=0
ADDED_FILES=0
CURRENT_SCOPE=pending
FAILED_PATH=none
ARCHIVE_MD5=unavailable
GZIP_FOOTER=unavailable

remove_self() {
    if [ "$0" = "$SELF_PATH" ] && [ -f "$SELF_PATH" ] && [ ! -L "$SELF_PATH" ]; then
        if grep -F -q "# ACC_ECU_COPY_TOKEN=$COPY_TOKEN" "$SELF_PATH"; then
            rm -f "$SELF_PATH"
        fi
    fi
}
trap remove_self 0
[ -d "$WEB_BASE" ] || exit 10
mkdir -m 755 "$WEB_DIR" || exit 12

write_status() {
    {
        printf 'TOKEN=%s\nSTATE=%s\nREASON=%s\n' "$COPY_TOKEN" "$1" "$2"
        printf 'BYTES=%s\nSHA256=%s\n' "${3:-0}" "${4:-unavailable}"
        printf 'METHOD=extended_code_pax\nTOTAL_SOURCE_BYTES=%s\nFILES=%s\n' "$TOTAL_BYTES" "$FILE_COUNT"
        printf 'TMP_FREE_KIB=%s\nWORK_FREE_KIB=%s\nWEB_FREE_KIB=%s\n' "$FREE_TMP" "$FREE_WORK" "$FREE_WEB"
        printf 'REQUIRED_WEB_KIB=%s\nREQUIRED_WORK_KIB=%s\nDISK_RESERVE_KIB=32768\n' "$REQUIRED_WEB" "$REQUIRED_WORK"
        printf 'SOURCE_CHECK=%s\nMD5=%s\nGZIP_FOOTER=%s\n' "$SOURCE_CHECK" "$ARCHIVE_MD5" "$GZIP_FOOTER"
        printf 'TRANSPORT=%s\nPACKED_FILES=%s\nPACK_TOTAL=%s\nPHASE_DONE=%s\n' "$TRANSPORT" "$PACKED_FILES" "$PACK_TOTAL" "$PHASE_DONE"
        printf 'SCAN_SCOPE=%s\nSCANNED_FILES=%s\nADDED_FILES=%s\nFAILED_PATH=%s\n' "$CURRENT_SCOPE" "$WALK_FILES" "$ADDED_FILES" "$FAILED_PATH"
    } > "$WEB_DIR/status.part"
    chmod 644 "$WEB_DIR/status.part"
    mv "$WEB_DIR/status.part" "$WEB_DIR/status.txt"
}
finish() {
    if [ "$COPY_STATE" != READY ]; then
        write_status FAILED "$COPY_REASON" || :
        rm -f "$WEB_DIR/transport.part" "$WEB_DIR/archive.part" "$WEB_DIR/ecu_etendu.tar" "$WEB_DIR/ecu_etendu.tar.gz"
    fi
    if [ "$OWN_WORK" = 1 ]; then
        # This directory was exclusively created by this run.
        rm -rf "$WORK_DIR"
    fi
    remove_self
    # Retention starts AFTER collection finishes, so a large archive does not
    # consume the user's download window while it is being compressed.
    (
        trap - 0 1 2 15
        sleep "$RETENTION"
        rm -f "$WEB_DIR/status.txt" "$WEB_DIR/status.part" "$WEB_DIR/transport.part" "$WEB_DIR/ecu_etendu.tar" \
              "$WEB_DIR/inventaire.tsv" "$WEB_DIR/inventaire.part" \
              "$WEB_DIR/ecu_etendu.tar.gz" "$WEB_DIR/archive.part"
        rmdir "$WEB_DIR" 2>/dev/null || :
    ) </dev/null >/dev/null 2>&1 &
}
trap finish 0
trap 'COPY_REASON=interrupted; exit 13' 1 2 15
write_status WORKING inventory
[ -d "$WORK_BASE" ] || { COPY_REASON=work_base_unavailable; exit 15; }
free_kib() {
    'df' -Pk "$1" | awk 'END {print $4}'
}
FREE_TMP=$(free_kib "$SOURCE_ROOT/tmp")
FREE_WORK=$(free_kib "$WORK_BASE")
FREE_WEB=$(free_kib "$WEB_BASE")
case "$FREE_WORK:$FREE_WEB" in *[!0-9:]*|:*|*:) COPY_REASON=unknown_free_space; exit 16;; esac
for cmd in cp mkdir mv rm rmdir sleep wc df awk chmod cmp grep sh dd od tr readlink cat; do
    command -v "$cmd" >/dev/null 2>&1 || {
        COPY_REASON="missing_tool_$cmd"; exit 14
    }
done
[ "$FREE_WORK" -ge "$REQUIRED_WORK" ] || { COPY_REASON=work_snapshot_space_insufficient; exit 17; }
[ "$FREE_WEB" -ge 1536 ] || { COPY_REASON=web_metadata_space_insufficient; exit 18; }
mkdir -m 700 "$WORK_DIR" || { COPY_REASON=work_path_exists; exit 19; }
OWN_WORK=1
mkdir "$CONTROL" "$WORK_DIR/payload" "$WORK_DIR/runtime"
: > "$WORK_DIR/copied_paths.txt"
: > "$WORK_DIR/missing_paths.txt"
: > "$WORK_DIR/excluded_paths.tsv"
: > "$WORK_DIR/symlinks.tsv"
: > "$WORK_DIR/scopes.txt"

: > "$CONTROL/members"
WORK_SINCE_DF=0

make_plan() {
    awk -v zero_digits="$ZERO_DIGITS" -v error_file="$CONTROL/plan.error" -v totals_file="$CONTROL/totals" '
# Produce an ASCII plan. The shell writes each header with printf %b, then
# the file bytes and padding. No binary bytes pass through awk or shell variables.
BEGIN {
    FS = "\t"
    zero_repr = zero_digits == 3 ? "\\000" : "\\0000"
    for (i = 32; i <= 126; i++) ord[sprintf("%c", i)] = i
    for (i = 1; i <= 512; i++) zeros = zeros "~"
    ord["~"] = 0
}
function fail(message) {
    print message > error_file
    failed = 1
    exit 1
}
function pad(value, width) {
    if (length(value) > width) fail("header_field_too_long")
    return value substr(zeros, 1, width - length(value))
}
function header(name, size, perm, kind,   raw,total,j) {
    raw = pad(name,100) pad(perm,8) pad("0000000",8) pad("0000000",8)
    raw = raw pad(sprintf("%011o",size),12) pad("00000000000",12) "        " kind
    raw = raw substr(zeros,1,100) "ustar~00" substr(zeros,1,64)
    raw = raw pad("0000000",8) pad("0000000",8) substr(zeros,1,167)
    if (length(raw) != 512) fail("invalid_tar_header_size")
    total = 0
    for (j=1; j<=512; j++) total += ord[substr(raw,j,1)]
    return substr(raw,1,148) sprintf("%06o",total) "~ " substr(raw,157)
}
function escaped(value,    out,i,ch) {
    out = ""
    for (i=1; i<=length(value); i++) {
        ch = substr(value,i,1)
        if (ch == "~") out = out zero_repr
        else if (ch == "|") out = out "\\n"
        else out = out ch
    }
    return out
}
{
    if (NF != 3 || $1 !~ /^[0-9]+$/ || $1+0 > 67108864 || $2 !~ /^(0000644|0000755)$/)
        fail("invalid_tar_member")
    path = $3
    if (path !~ /^[A-Za-z0-9_.+\/-]+$/ || path ~ /(^|\/)\.\.?($|\/)/ || path ~ /(^\/|\/$|\/\/)/ || length(path)>4095 || seen[path]++)
        fail("invalid_or_duplicate_tar_path")
    sz = $1+0
    name = path
    prefix = ""
    if (length(path) > 100) {
        # POSIX pax length includes the decimal length, space and final LF.
        body = " path=" path "|"
        n = length(body)+1
        while (n != length(body)+length(n "")) n = length(body)+length(n "")
        rec = n body
        prefix = header("PaxHeaders/entry",length(rec),"0000644","x") rec
        prefix = prefix substr(zeros,1,(512 - length(rec) % 512) % 512)
        name = "pax-member-" NR
    }
    prefix = prefix header(name,sz,$2,"0")
    tail = (512 - sz % 512) % 512
    printf "%s\t%s\t%s\n", path,escaped(prefix),escaped(substr(zeros,1,tail))
    bytes += length(prefix) + sz + tail
}
END {
    if (!failed) {
        if (NR < 1) fail("empty_tar_plan")
        printf "%.0f\n%d\n", bytes+1024, NR > totals_file
    }
}

    ' "$1" > "$CONTROL/plan"
}

file_size() {
    fs_value=$(wc -c < "$1") || return 1
    set -- $fs_value
    [ "$#" = 1 ] || return 1
    case "$1" in ''|*[!0-9]*) return 1;; esac
    printf '%s\n' "$1"
}

packing_self_test() {
    # Old printf applets differ in how many octal digits they consume after \0.
    # Test a zero followed by an ordinary digit and a newline on this shell.
    ZERO_DIGITS=4
    ZERO_REPR='\0000'
    probe=$(printf '%b' "Q${ZERO_REPR}7\n" | od -An -tx1 | tr -d ' \n')
    if [ "$probe" != 5100370a ]; then
        ZERO_DIGITS=3
        ZERO_REPR='\000'
        probe=$(printf '%b' "Q${ZERO_REPR}7\n" | od -An -tx1 | tr -d ' \n')
        [ "$probe" = 5100370a ] || return 1
    fi
    printf '0\t0000644\tprobe\n' > "$CONTROL/probe"
    make_plan "$CONTROL/probe" || return 1
    IFS="$TAB" read -r probe_name probe_header probe_pad < "$CONTROL/plan" || return 1
    [ "$probe_name" = probe ] || return 1
    printf '%b' "$probe_header" > "$CONTROL/header" || return 1
    [ "$(file_size "$CONTROL/header")" = 512 ] || return 1
    # Check the bytes independently, including the octal fields and all padding.
    if probe_hash=$(busybox sha256sum < "$CONTROL/header" 2>/dev/null); then
        set -- $probe_hash
        [ "$#" -gt 0 ] && [ "$1" = 206464a6a0db3281752542b8ec59d93b5b32e090c38df8e956fd4aa2de307600 ] && return 0
    fi
    od -An -v -tu1 "$CONTROL/header" > "$CONTROL/header.dec" || return 1
    awk '{for (i=1;i<=NF;i++) {n++; sum=(sum*31+$i)%65521}} END {exit !(n==512 && sum==57039)}' "$CONTROL/header.dec"
}

packing_self_test || { COPY_REASON=archive_writer_self_test_failed; exit 14; }

exclude() {
    # Escape control characters in names, rather than let them become new rows.
    excluded_name=$(printf '%s' "$2" | tr '\n\r\t' '???')
    printf '%s\t%s\n' "$1" "$excluded_name" >> "$WORK_DIR/excluded_paths.tsv"
}

collect_file() {
    collect_source=$1
    collect_relative=$2
    collect_required=$3
    collect_mode=$4
    collect_target="$WORK_DIR/$collect_relative"
    [ ! -f "$collect_target" ] || return 0
    case "$collect_relative" in
        runtime/*/executable) :;;
        *) if [ -L "$collect_source" ]; then
               exclude symbolic_link_not_followed "$collect_relative"
               if [ "$collect_required" = required ]; then
                   FAILED_PATH=$collect_relative; COPY_REASON=required_file_is_symlink; exit 20
               fi
               return 0
           fi;;
    esac
    if [ ! -f "$collect_source" ] || [ ! -r "$collect_source" ]; then
        printf '%s\n' "$collect_relative" >> "$WORK_DIR/missing_paths.txt"
        if [ "$collect_required" = required ]; then
            FAILED_PATH=$collect_relative; COPY_REASON=required_file_unreadable; exit 20
        fi
        return 0
    fi
    FAILED_PATH=$collect_relative
    collect_size=$(file_size "$collect_source") || { COPY_REASON=source_size_unreadable; exit 21; }
    if [ "$collect_size" -gt "$MAX_FILE_BYTES" ]; then
        exclude file_over_64_MiB "$collect_relative"
        if [ "$collect_required" = required ]; then COPY_REASON=required_file_over_limit; exit 22; fi
        FAILED_PATH=none; return 0
    fi
    [ "$((TOTAL_BYTES + collect_size))" -le "$MAX_BYTES" ] || { COPY_REASON=copy_limit_256_MiB; exit 22; }
    [ "$FILE_COUNT" -lt "$MAX_FILES" ] || { COPY_REASON=copy_limit_12000_files; exit 22; }
    if [ "$((FILE_COUNT % 32))" -eq 0 ] || [ "$collect_size" -ge 4194304 ]; then
        FREE_WORK=$(free_kib "$WORK_BASE")
        case "$FREE_WORK" in ''|*[!0-9]*) COPY_REASON=unknown_free_space; exit 16;; esac
        WORK_SINCE_DF=0
    fi
    work_cost=$(((collect_size + 1023) / 1024 + 12))
    REQUIRED_WORK=$((40960 + WORK_SINCE_DF + work_cost))
    [ "$FREE_WORK" -ge "$REQUIRED_WORK" ] || { COPY_REASON=work_snapshot_space_insufficient; exit 17; }
    COPY_REASON=source_copy_failed
    mkdir -p "$WORK_DIR/${collect_relative%/*}"
    if ! cat "$collect_source" > "$collect_target"; then
        case "$collect_relative" in
            runtime/*) if [ ! -f "$collect_source" ]; then
                rm -f "$collect_target"
                printf '%s\n' "$collect_relative" >> "$WORK_DIR/missing_paths.txt"
                FAILED_PATH=none; COPY_REASON=unexpected_error; return 0
            fi;;
        esac
        exit 35
    fi
    actual_size=$(file_size "$collect_target") || { COPY_REASON=snapshot_size_unreadable; exit 21; }
    if [ "$collect_mode" = snapshot ]; then
        SNAPSHOT_BYTES=$((SNAPSHOT_BYTES + actual_size))
        [ "$actual_size" -le 262144 ] && [ "$SNAPSHOT_BYTES" -le 1048576 ] || {
            COPY_REASON=metadata_limit_exceeded; exit 23
        }
    else
        [ "$actual_size" = "$collect_size" ] || { COPY_REASON=source_size_changed_during_copy; exit 35; }
    fi
    [ "$actual_size" -le "$MAX_FILE_BYTES" ] || { COPY_REASON=file_changed_over_limit; exit 22; }
    TOTAL_BYTES=$((TOTAL_BYTES + actual_size))
    [ "$TOTAL_BYTES" -le "$MAX_BYTES" ] || { COPY_REASON=copy_limit_256_MiB; exit 22; }
    FILE_COUNT=$((FILE_COUNT + 1))
    WORK_SINCE_DF=$((WORK_SINCE_DF + (actual_size + 1023) / 1024 + 12))
    collect_perm=0000644
    if [ -x "$collect_source" ]; then collect_perm=0000755; fi
    printf '%s\t%s\t%s\n' "$actual_size" "$collect_perm" "$collect_relative" >> "$CONTROL/members"
    printf '%s\n' "$collect_relative" >> "$WORK_DIR/copied_paths.txt"
    COPY_REASON=unexpected_error
    FAILED_PATH=none
    if [ "$((FILE_COUNT % 32))" -eq 1 ]; then write_status WORKING inventory; fi
}

collect_file "$SOURCE_ROOT/etc/yuneng/version.conf" payload/etc/yuneng/version.conf required snapshot
for relative in home/applications/main.exe home/applications/meterManager home/applications/ipcTest; do
    collect_file "$SOURCE_ROOT/$relative" "payload/$relative" required file
done
for relative in \
    home/applications/meterManagerMonitor \
    home/applications/resmonitor \
    lib/libmetertools.so \
    usr/lib/libmetertools.so \
    home/applications/libmetertools.so \
    lib/libYNtools.so \
    home/local_web/pages/application/models/hidden_model.php \
    home/local_web/pages/application/models/management_model.php \
    home/local_web/pages/application/controllers/hidden.php \
    home/local_web/pages/application/controllers/management.php \
    home/local_web/pages/application/views/hidden/debug.php \
    home/local_web/pages/application/views/hidden/upload.php \
    home/local_web/pages/application/views/management/upgrade_ecu_with_check.php \
    home/local_web/pages/application/config/routes.php; do
    collect_file "$SOURCE_ROOT/$relative" "payload/$relative" optional file
done

# Collect each entry returned by the shell directly. All file and directory names
# stay quoted until after the path policy has been checked.
printf 'scope\tstate\tdirectories\tregular_files\tlinks\tadded_files\n' > "$WORK_DIR/inventaire.tsv"
: > "$WORK_DIR/ajouts.txt"
: > "$WORK_DIR/ancres.txt"

newline='
'
mounts=''
if [ -r "$SOURCE_ROOT/proc/self/mountinfo" ]; then
    mounts=$(awk '$5 ~ /^\// {print $5}' "$SOURCE_ROOT/proc/self/mountinfo") || mounts=''
elif [ -r "$SOURCE_ROOT/proc/mounts" ]; then
    mounts=$(awk '$2 ~ /^\// {print $2}' "$SOURCE_ROOT/proc/mounts") || mounts=''
fi
mounts="$newline$mounts$newline"
case "$mounts" in *"$newline/$newline"*) boundary=mounts;; *) boundary=df;; esac

inventory_fail() {
    COPY_REASON=$1
    FAILED_PATH=$2
    exit 32
}

filesystem_for() {
    'df' -Pk "$1" | awk 'END {print $1}'
}

record_link() {
    link_target=$(readlink "$1") || inventory_fail link_unreadable "$2"
    link_target=$(printf '%s' "$link_target" | tr '\n\r\t' '???')
    printf '%s\t%s\n' "$2" "$link_target" >> "$WORK_DIR/symlinks.tsv"
}

select_regular() {
    # $1 is the physical file. $2 is the validated ECU-relative path.
    case "$2" in
        home/applications/main.exe|home/applications/meterManager|home/applications/ipcTest|home/local_web/pages/application/models/hidden_model.php|lib/libmetertools.so)
            printf '%s\n' "$2" >> "$WORK_DIR/ancres.txt";;
    esac
    if [ -e "$WORK_DIR/payload/$2" ] || [ -L "$WORK_DIR/payload/$2" ]; then return 0; fi
    case "$2" in
        */config/*|*/configuration/*|*/.env*|*/.htpasswd*|*/id_rsa*|*/id_ed25519*|*/passwd|*/shadow|*/password*|*/secret*|*/credential*|*.key|*.pem|*.p12|*.pfx|*.db|*.db-*|*.sqlite*|*.log|*.conf|*.ini|*.cfg|*.bak|*.old|*.zip|*.tar|*.gz|*.bz2|*.img|*~)
            exclude configuration_data_or_archive "/$2"; return 0;;
    esac
    case "$2" in
        home/local_web/*)
            case "$2" in
                *.php|*.js|*.css|*.html|*.htm|*.tpl|*.inc|*.svg|*.png|*.jpg|*.jpeg|*.gif|*.ico|*.woff|*.woff2|*.ttf|*.eot|*.map) :;;
                *) exclude outside_code_selection "/$2"; return 0;;
            esac;;
        etc/init.d/*|*.sh|*.py|*.pl|*.so|*.so.*|*.ko|*.a) :;;
        *)
            magic=$(dd if="$1" bs=4 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
            case "$magic" in 7f454c46|2321*) :;; *) exclude outside_code_selection "/$2"; return 0;; esac;;
    esac
    count_before=$FILE_COUNT
    collect_file "$1" "payload/$2" optional link
    if [ "$FILE_COUNT" -gt "$count_before" ]; then
        ADDED_FILES=$((ADDED_FILES + 1))
        printf 'payload/%s\n' "$2" >> "$WORK_DIR/ajouts.txt"
    fi
}

scan_scope() {
    CURRENT_SCOPE="/$1"
    scope_root="$SOURCE_ROOT$CURRENT_SCOPE"
    scope_dirs=0
    scope_files=0
    scope_links=0
    scope_prior=$ADDED_FILES
    scope_state=visited
    printf '%s\n' "$CURRENT_SCOPE" >> "$WORK_DIR/scopes.txt"
    write_status WORKING inventory
    if [ -L "$scope_root" ]; then
        scope_state=symlink
        record_link "$scope_root" "$CURRENT_SCOPE"
        exclude scope_is_symlink "$CURRENT_SCOPE"
    elif [ ! -d "$scope_root" ]; then
        scope_state=missing
        printf '%s\n' "$CURRENT_SCOPE" >> "$WORK_DIR/missing_paths.txt"
    else
        if [ "$boundary" = df ]; then
            root_fs=$(filesystem_for "$scope_root")
            [ -n "$root_fs" ] || inventory_fail filesystem_unknown "$CURRENT_SCOPE"
        fi
        # Read one queue and write the next one, so no shell recursion or read/append race.
        queue_bytes=0
        printf '%s\n' "$1" > "$WORK_DIR/dirs.part"
        while [ -s "$WORK_DIR/dirs.part" ]; do
            : > "$WORK_DIR/next-dirs.part"
            while IFS= read -r parent; do
                folder="$SOURCE_ROOT/$parent"
                [ -d "$folder" ] && [ ! -L "$folder" ] && [ -r "$folder" ] && [ -x "$folder" ] || inventory_fail directory_unreadable "/$parent"
                scope_dirs=$((scope_dirs + 1))
                for entry in "$folder"/* "$folder"/.[!.]* "$folder"/..?*; do
                    [ -e "$entry" ] || [ -L "$entry" ] || continue
                    rel=${entry#"$SOURCE_ROOT"/}
                    WALK_ENTRIES=$((WALK_ENTRIES + 1))
                    [ "$WALK_ENTRIES" -le 75000 ] || inventory_fail inventory_entry_limit "$CURRENT_SCOPE"
                    case "$rel" in *[!A-Za-z0-9_./+-]*) exclude unsupported_filename "/$rel"; continue;; esac
                    case "$rel" in "$1"/*) :;; *) inventory_fail invalid_inventory_path "$CURRENT_SCOPE";; esac
                    if [ -L "$entry" ]; then
                        scope_links=$((scope_links + 1))
                        record_link "$entry" "/$rel"
                        continue
                    fi
                    if [ "$boundary" = mounts ]; then
                        case "$mounts" in *"$newline/$rel$newline"*) exclude nested_mount "/$rel"; continue;; esac
                    fi
                    if [ -d "$entry" ]; then
                        case "${entry##*/}" in
                            .git|.ssh|cache|logs|sessions|uploads|backups|backup|certificates|certs|keys|acc_ecu_copy_*) exclude pruned_directory "/$rel"; continue;;
                        esac
                        if [ "$boundary" = df ]; then
                            nested_fs=$(filesystem_for "$entry")
                            [ -n "$nested_fs" ] || inventory_fail filesystem_unknown "/$rel"
                            if [ "$nested_fs" != "$root_fs" ]; then exclude nested_mount "/$rel"; continue; fi
                        fi
                        queue_bytes=$((queue_bytes + ${#rel} + 1))
                        [ "$queue_bytes" -le 4194304 ] || inventory_fail directory_queue_limit "$CURRENT_SCOPE"
                        printf '%s\n' "$rel" >> "$WORK_DIR/next-dirs.part"
                    elif [ -f "$entry" ]; then
                        scope_files=$((scope_files + 1))
                        WALK_FILES=$((WALK_FILES + 1))
                        select_regular "$entry" "$rel"
                    fi
                done
            done < "$WORK_DIR/dirs.part"
            mv "$WORK_DIR/next-dirs.part" "$WORK_DIR/dirs.part"
        done
        rm -f "$WORK_DIR/dirs.part"
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$CURRENT_SCOPE" "$scope_state" "$scope_dirs" "$scope_files" "$scope_links" "$((ADDED_FILES - scope_prior))" >> "$WORK_DIR/inventaire.tsv"
    # Counts survive a later failure and are readable by the PC during the download window.
    cp "$WORK_DIR/inventaire.tsv" "$WEB_DIR/inventaire.part"
    chmod 644 "$WEB_DIR/inventaire.part"
    mv "$WEB_DIR/inventaire.part" "$WEB_DIR/inventaire.tsv"
}

for scope in home/applications home/local_web lib usr/lib bin sbin usr/bin usr/sbin usr/local/bin usr/local/sbin etc/init.d; do
    scan_scope "$scope"
done
CURRENT_SCOPE=coverage
# A preselected file proves the path was readable, but must also be seen by the walker.
for anchor in home/applications/main.exe home/applications/meterManager home/applications/ipcTest home/local_web/pages/application/models/hidden_model.php lib/libmetertools.so; do
    if [ -f "$WORK_DIR/payload/$anchor" ]; then
        grep -F -x -q "$anchor" "$WORK_DIR/ancres.txt" || inventory_fail known_file_missed_by_inventory "/$anchor"
    fi
done
[ "$ADDED_FILES" -gt 0 ] || inventory_fail no_additional_files all_scopes
actual_additions=$(wc -l < "$WORK_DIR/ajouts.txt" | awk '{print $1}')
[ "$actual_additions" = "$ADDED_FILES" ] || inventory_fail inconsistent_inventory all_scopes
# These three regular library paths were mapped on this unit in the v3.1 capture.
for witness in lib/ld-2.15.so lib/libc-2.15.so usr/lib/libz.so.1.2.7; do
    if [ -f "$SOURCE_ROOT/$witness" ] && [ ! -L "$SOURCE_ROOT/$witness" ] && [ -r "$SOURCE_ROOT/$witness" ]; then
        [ -f "$WORK_DIR/payload/$witness" ] || inventory_fail mapped_library_missed "/$witness"
    fi
done
CURRENT_SCOPE=complete


for relative in etc/os-release etc/issue proc/version proc/partitions proc/mtd; do
    collect_file "$SOURCE_ROOT/$relative" "system/$relative" optional snapshot
done

# Snapshot the process executable now. Its PID can disappear before serialization.
for comm_file in "$SOURCE_ROOT"/proc/[0-9]*/comm; do
    [ -r "$comm_file" ] || continue
    process_name=''
    IFS= read -r process_name < "$comm_file" || continue
    case "$process_name" in main.exe|meterManager|ipcTest|meterManagerMon|resmonitor) :;; *) continue;; esac
    process_dir=${comm_file%/comm}
    process_pid=${process_dir##*/}
    collect_file "$comm_file" "runtime/$process_pid/comm" optional snapshot
    collect_file "$process_dir/maps" "runtime/$process_pid/maps" optional snapshot
    collect_file "$process_dir/status" "runtime/$process_pid/status" optional snapshot
    collect_file "$process_dir/stat" "runtime/$process_pid/stat" optional snapshot
    collect_file "$process_dir/wchan" "runtime/$process_pid/wchan" optional snapshot
    collect_file "$process_dir/exe" "runtime/$process_pid/executable" optional file
    if [ -r "$WORK_DIR/runtime/$process_pid/maps" ]; then
        awk '$NF ~ /\/libmetertools\.so(\.[0-9]+)*$/ {print $NF}' "$WORK_DIR/runtime/$process_pid/maps" \
            > "$WORK_DIR/runtime/$process_pid/libmetertools_paths.txt"
        while IFS= read -r library_path; do
            case "$library_path" in /lib/*|/usr/lib/*|/home/applications/*) :;; *) continue;; esac
            case "$library_path" in *'/../'*|*'/./'*) continue;; esac
            collect_file "$SOURCE_ROOT$library_path" "payload${library_path}" optional file
        done < "$WORK_DIR/runtime/$process_pid/libmetertools_paths.txt"
    fi
done

collect_file "$SOURCE_ROOT/etc/yuneng/version.conf" runtime/version_after.conf required snapshot
cmp -s "$WORK_DIR/payload/etc/yuneng/version.conf" "$WORK_DIR/runtime/version_after.conf" || {
    COPY_REASON=version_changed_during_inventory; exit 24
}
# Choose the strongest working on-device checksum.
source_value() {
    case "$SOURCE_CHECK" in
        sha256sum) raw=$(sha256sum < "$1") || return 1; value=${raw%% *}; digits=64;;
        busybox_sha256) raw=$(busybox sha256sum < "$1") || return 1; value=${raw%% *}; digits=64;;
        openssl_sha256) raw=$(openssl dgst -sha256 < "$1") || return 1; value=${raw##* }; digits=64;;
        md5sum) raw=$(md5sum < "$1") || return 1; value=${raw%% *}; digits=32;;
        cksum)
            raw=$(cksum < "$1") || return 1
            set -- $raw
            [ "$#" -eq 2 ] || return 1
            case "$1:$2" in *[!0-9:]*|:*|*:) return 1;; esac
            printf '%s %s\n' "$1" "$2"; return;;
        *) return 1;;
    esac
    [ "${#value}" -eq "$digits" ] || return 1
    case "$value" in *[!0-9a-f]*) return 1;; esac
    printf '%s\n' "$value"
}

# A working known-answer probe also rules out broken binaries / missing applets.
for SOURCE_CHECK in sha256sum busybox_sha256 openssl_sha256 md5sum cksum; do
    case "$SOURCE_CHECK" in
        md5sum) expected=d41d8cd98f00b204e9800998ecf8427e; manifest=MD5SUMS.txt;;
        cksum) expected='4294967295 0'; manifest=CKSUMS.txt;;
        *) expected=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855; manifest=SHA256SUMS.txt;;
    esac
    if actual=$(source_value /dev/null 2>/dev/null) && [ "$actual" = "$expected" ]; then break; fi
    SOURCE_CHECK=cmp
done

# Each zero in the ASCII plan consumes up to five bytes. Bound the plan before
# creating it, including pax records for long names and the metadata reports.
PLAN_KIB=$(awk -F '\t' '{n+=18000+5*length($3)} END {printf "%.0f",n/1024+1024}' "$CONTROL/members")
REQUIRED_WORK=$((32768 + PLAN_KIB))
FREE_WORK=$(free_kib "$WORK_BASE")
case "$FREE_WORK" in ''|*[!0-9]*) COPY_REASON=unknown_free_space; exit 16;; esac
[ "$FREE_WORK" -ge "$REQUIRED_WORK" ] || { COPY_REASON=work_plan_space_insufficient; exit 17; }
TRANSPORT=tar
case "$SOURCE_CHECK" in
    cksum|cmp)
        command -v 'gzip' >/dev/null 2>&1 || { COPY_REASON=missing_tool_gzip_for_crc; exit 14; }
        TRANSPORT=gzip;;
esac
{
    printf 'COPY_TOKEN=%s\nMETHOD=extended_code_pax\n' "$COPY_TOKEN"
    printf 'NO_FLASH_DUMP=1\nNO_BULK_TMP_STAGING=1\n'
    printf 'FILES=%s\nSOURCE_BYTES=%s\nMAX_SOURCE_BYTES=%s\n' "$FILE_COUNT" "$TOTAL_BYTES" "$MAX_BYTES"
    printf 'WALKER=shell_globs\nSCANNED_FILES=%s\nADDED_FILES=%s\nMOUNT_BOUNDARY=%s\n' "$WALK_FILES" "$ADDED_FILES" "$boundary"
    printf 'SOURCE_CHECK=%s\nREQUIRED_WORK_KIB=%s\n' "$SOURCE_CHECK" "$REQUIRED_WORK"
    printf 'SYMLINKS=recorded_not_followed\nKNOWN_CREDENTIAL_CONFIGS=excluded\n'
    printf 'TMP_FREE_KIB=%s\nWEB_FREE_KIB_AT_START=%s\nTRANSPORT=%s\n' "$FREE_TMP" "$FREE_WEB" "$TRANSPORT"
    date -u '+UTC=%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || :
    uname -a 2>/dev/null || :
    id 2>/dev/null || :
} > "$WORK_DIR/capture.txt"

cat > "$WORK_DIR/LIMITES.txt" <<'LIMITS'
Collecte ciblee du code, pas une sauvegarde complete ni un dump flash.
Racines visitees : scopes.txt. Fichiers effectivement copies : copied_paths.txt.
inventaire.tsv donne les repertoires, fichiers ordinaires, liens et ajouts par racine.
ajouts.txt donne les nouveaux chemins au-dela de la liste fixe. ancres.txt donne les temoins retrouves.
Les liens symboliques figurent dans symlinks.tsv. Les montages imbriques sont ecartes
selon la table des montages, ou la frontiere de volume via df si elle est indisponible.
Repertoires non parcourus : .git, .ssh, cache, logs, sessions, uploads,
backups, backup, certificates, certs, keys et acc_ecu_copy_*.
Autres exclusions : excluded_paths.tsv. Configurations, bases, archives,
cles et fichiers d'identifiants reconnus ne sont pas collectes.
Exception de configuration : routes.php et version.conf sont inclus.
Seuls les noms ASCII lettres/chiffres et _ . / + - sont acceptes.
Les programmes et scripts peuvent eux-memes contenir des secrets integres :
cette selection ne constitue pas un nettoyage de toutes les donnees sensibles.
Les fichiers proc sont des releves ponctuels, pas une capture continue.
Le champ SOURCE_CHECK de capture.txt indique le controle effectivement choisi.
Les fichiers collectes sont des copies physiques privees. Les executables /proc
sont lus pendant le releve. Les sources payload sont comparees aux copies.
TAR POSIX, en-tete pax pour les noms longs. Modes 0644 ou 0755 selon les droits
d'execution, date et identifiants normalises. Usage prevu : analyse du code.
LIMITS

PHASE_DONE=0
write_status WORKING verifying_snapshots
if [ "$SOURCE_CHECK" = cmp ]; then manifest=SOURCES_CMP.tsv; fi
: > "$WORK_DIR/$manifest"
while IFS= read -r name; do
    FAILED_PATH=$name
    case "$name" in
        payload/*)
            original="$SOURCE_ROOT/${name#payload/}"
            [ ! -L "$original" ] && [ -f "$original" ] && cmp -s "$original" "$WORK_DIR/$name" || {
                COPY_REASON=source_changed_since_snapshot; exit 30
            }
            if [ "$SOURCE_CHECK" = cmp ]; then
                printf '%s\t%s\n' "$name" "${name#payload/}" >> "$WORK_DIR/$manifest"
            fi;;
    esac
    if [ "$SOURCE_CHECK" != cmp ]; then
        digest=$(source_value "$WORK_DIR/$name") || { COPY_REASON=source_checksum_failed; exit 35; }
        printf '%s  %s\n' "$digest" "$name" >> "$WORK_DIR/$manifest"
    fi
    PHASE_DONE=$((PHASE_DONE + 1))
    if [ "$((PHASE_DONE % 32))" -eq 0 ]; then write_status WORKING verifying_snapshots; fi
done < "$WORK_DIR/copied_paths.txt"
[ "$PHASE_DONE" = "$FILE_COUNT" ] || { COPY_REASON=snapshot_count_mismatch; exit 35; }
FAILED_PATH=none
# Add only declared reports. Do not recurse through a staging directory.
for meta in capture.txt LIMITES.txt copied_paths.txt missing_paths.txt excluded_paths.tsv \
            symlinks.tsv scopes.txt inventaire.tsv ajouts.txt ancres.txt "$manifest" \
            "$WORK_DIR"/runtime/[0-9]*/libmetertools_paths.txt; do
    case "$meta" in
        "$WORK_DIR"/*) [ -f "$meta" ] || continue; meta=${meta#"$WORK_DIR"/};;
    esac
    FAILED_PATH=$meta
    meta_size=$(file_size "$WORK_DIR/$meta") || { COPY_REASON=report_unreadable; exit 35; }
    printf '%s\t0000644\t%s\n' "$meta_size" "$meta" >> "$CONTROL/members"
done
FAILED_PATH=none
write_status WORKING preparing_headers
make_plan "$CONTROL/members" || {
    COPY_REASON=invalid_archive_plan
    if [ -s "$CONTROL/plan.error" ]; then IFS= read -r COPY_REASON < "$CONTROL/plan.error" || :; fi
    exit 28
}
{
    IFS= read -r TAR_BYTES
    IFS= read -r PACK_TOTAL
} < "$CONTROL/totals"
case "$TAR_BYTES:$PACK_TOTAL" in *[!0-9:]*|:*|*:) COPY_REASON=invalid_archive_plan; exit 28;; esac
[ "$TAR_BYTES" -le 536870912 ] && [ "$PACK_TOTAL" -ge "$FILE_COUNT" ] || { COPY_REASON=archive_size_invalid; exit 28; }
REQUIRED_WEB=$(((TAR_BYTES + 1023) / 1024 + 40960))
if [ "$TRANSPORT" = gzip ]; then REQUIRED_WEB=$((REQUIRED_WEB + (TAR_BYTES + 1023) / 1024 + 1024)); fi
FREE_WEB=$(free_kib "$WEB_BASE")
case "$FREE_WEB" in ''|*[!0-9]*) COPY_REASON=unknown_free_space; exit 25;; esac
[ "$FREE_WEB" -ge "$REQUIRED_WEB" ] || { COPY_REASON=web_space_insufficient; exit 26; }

write_status WORKING writing_archive
emit_tar() {
    while IFS="$TAB" read -r name prefix padding; do
        FAILED_PATH=$name
        printf '%b' "$prefix" || { COPY_REASON=archive_header_write_failed; exit 28; }
        cat "$WORK_DIR/$name" || { COPY_REASON=archive_member_write_failed; exit 28; }
        printf '%b' "$padding" || { COPY_REASON=archive_padding_write_failed; exit 28; }
        PACKED_FILES=$((PACKED_FILES + 1))
        if [ "$((PACKED_FILES % 24))" -eq 1 ]; then write_status WORKING writing_archive; fi
    done < "$CONTROL/plan"
    terminator=$ZERO_REPR
    for shift_count in 1 2 3 4 5 6 7 8 9 10; do terminator="$terminator$terminator"; done
    printf '%b' "$terminator" || { COPY_REASON=archive_end_write_failed; exit 28; }
}
COPY_REASON=archive_open_failed
emit_tar > "$WEB_DIR/archive.part"
FAILED_PATH=none
[ "$PACKED_FILES" = "$PACK_TOTAL" ] && [ "$(file_size "$WEB_DIR/archive.part")" = "$TAR_BYTES" ] || {
    COPY_REASON=archive_length_mismatch; exit 28
}
cmp -s "$WORK_DIR/payload/etc/yuneng/version.conf" "$SOURCE_ROOT/etc/yuneng/version.conf" || {
    FAILED_PATH=payload/etc/yuneng/version.conf; COPY_REASON=version_changed_during_archive; exit 31
}
ARCHIVE_SHA=unavailable
ARCHIVE_SIZE=$TAR_BYTES
public_name=ecu_etendu.tar
if [ "$TRANSPORT" = gzip ]; then
    # On legacy images without SHA/MD5 use gzip CRC32 + length across the wire.
    # Both files are on the web filesystem, even if /home/update_from_app is on another mount.
    write_status WORKING compressing_for_crc
    'gzip' -1 -c < "$WEB_DIR/archive.part" > "$WEB_DIR/transport.part" || {
        COPY_REASON=compression_failed; exit 27
    }
    mv -f "$WEB_DIR/transport.part" "$WEB_DIR/archive.part"
    ARCHIVE_SIZE=$(file_size "$WEB_DIR/archive.part") || { COPY_REASON=archive_size_invalid; exit 36; }
    [ "$ARCHIVE_SIZE" -ge 18 ] && [ "$ARCHIVE_SIZE" -le 536870912 ] || { COPY_REASON=archive_size_invalid; exit 36; }
    dd if="$WEB_DIR/archive.part" of="$CONTROL/footer" bs=1 skip=$((ARCHIVE_SIZE - 8)) count=8 2>/dev/null || {
        COPY_REASON=gzip_footer_unreadable; exit 36
    }
    GZIP_FOOTER=$(od -An -tx1 "$CONTROL/footer" | tr -d ' \n')
    [ "${#GZIP_FOOTER}" -eq 16 ] || { COPY_REASON=gzip_footer_unreadable; exit 36; }
    public_name=ecu_etendu.tar.gz
else
    write_status WORKING hashing_archive
    hash=$(source_value "$WEB_DIR/archive.part") || { COPY_REASON=archive_hash_failed; exit 36; }
    case "$SOURCE_CHECK" in md5sum) ARCHIVE_MD5=$hash;; *) ARCHIVE_SHA=$hash;; esac
fi
COPY_REASON=publication_failed
chmod 644 "$WEB_DIR/archive.part"
mv "$WEB_DIR/archive.part" "$WEB_DIR/$public_name"
write_status READY complete "$ARCHIVE_SIZE" "$ARCHIVE_SHA"
COPY_STATE=READY
exit 0
