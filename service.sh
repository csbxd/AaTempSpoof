#!/system/bin/sh

MODDIR="${0%/*}"
BIN="$MODDIR/bin/AaTempSpoof"
CB="$MODDIR/bin/cb"
TOUCHBIN="$MODDIR/bin/touch_daemon"
TOMBSTONEBIN="$MODDIR/bin/color_tombstone"
CHARGEBIN="$MODDIR/bin/charge_horae_daemon"

CFGDIR="$MODDIR/AaTempSpoof"
PIDDIR="$MODDIR/pid"
LOG="$CFGDIR/tempspoof.log"

mkdir -p "$CFGDIR" "$PIDDIR"

PIDFILE="$PIDDIR/daemon.pid"
CBPID="$PIDDIR/cb.pid"
TOUCHPID="$PIDDIR/touch_daemon.pid"
TOMBSTONEPID="$PIDDIR/color_tombstone.pid"
CHARGEPID="$PIDDIR/charge_horae_daemon.pid"
NOTIFY_FILE="$PIDDIR/通知"

FLAGFILE="$PIDDIR/fake_batt_temp"
STOP_FLAG="$CFGDIR/user_stopped"

_log() {
    printf '[%s] [service] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1" >> "$LOG"
}

_sec() {
    local _ts
    _ts=$(date '+%Y-%m-%d %H:%M:%S')
    printf '\n' >> "$LOG"
    printf '[%s] [service] ┌─────────────────────────────────────┐\n' "$_ts" >> "$LOG"
    printf '[%s] [service] │  %-35s│\n' "$_ts" "$1" >> "$LOG"
    printf '[%s] [service] └─────────────────────────────────────┘\n' "$_ts" >> "$LOG"
}

_trim_pid() {
    tr -d ' \r\n' 2>/dev/null
}

_pid_alive() {
    local p
    [ -s "$1" ] || return 1
    p=$(cat "$1" 2>/dev/null | _trim_pid)
    case "$p" in
        ''|*[!0-9]*) rm -f "$1" 2>/dev/null; return 1 ;;
    esac
    kill -0 "$p" 2>/dev/null && return 0
    rm -f "$1" 2>/dev/null
    return 1
}

_find_pid() {
    local p
    p=$(pgrep -x "$1" 2>/dev/null | head -1)
    if [ -z "$p" ] && command -v pidof >/dev/null 2>&1; then
        p=$(pidof "$1" 2>/dev/null | awk '{print $1}')
    fi
    case "$p" in
        [0-9]*) printf '%s\n' "$p"; return 0 ;;
    esac
    return 1
}

_proc_alive() {
    local p
    _pid_alive "$1" && return 0
    p=$(_find_pid "$2")
    if [ -n "$p" ]; then
        printf '%s\n' "$p" > "$1" 2>/dev/null
        return 0
    fi
    return 1
}

_pid_value() {
    cat "$1" 2>/dev/null | _trim_pid
}

MODULE_NAME=$(grep "^name=" "$MODDIR/module.prop" 2>/dev/null | cut -d= -f2)
[ -z "$MODULE_NAME" ] && MODULE_NAME="模块加载失败"

notify() {
    su 2000 -c "cmd notification post -S bigtext -t \"$1\" tempspoof_action \"$2\"" 2>/dev/null
    _log "通知发送: 标题=\"$1\", 内容=\"$2\""
}

if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1048576 ]; then
    tail -c 65536 "$LOG" > "${LOG}.tmp"
    mv "${LOG}.tmp" "$LOG"
fi

rm -f "$LOG"
touch "$LOG" 2>/dev/null
chmod 0755 "$BIN" "$CB" "$TOUCHBIN" "$TOMBSTONEBIN" "$CHARGEBIN" 2>/dev/null

[ -x "$BIN" ] || { _log "致命: AaTempSpoof 不可执行"; exit 1; }

_sec "SERVICE"

_log "设备: $(getprop ro.product.model)"
_log "Android: $(getprop ro.build.version.release)"
_log "平台: $(getprop ro.board.platform)"

rm -f "$STOP_FLAG" "$FLAGFILE"

WAIT=0
while [ "$WAIT" -lt 120 ]; do
    [ "$(getprop sys.boot_completed)" = "1" ] && break
    sleep 5
    WAIT=$((WAIT + 5))
done

_log "等待 sysfs 热管理节点就绪..."
SYSFS_WAIT=0
while [ "$SYSFS_WAIT" -lt 30 ]; do
    if [ -e "/sys/class/thermal/thermal_zone0/emul_temp" ] || \
       [ -e "/sys/class/oplus_chg/battery/temp" ] || \
       [ -e "/sys/class/power_supply/battery/temp" ]; then
        break
    fi
    sleep 2
    SYSFS_WAIT=$((SYSFS_WAIT + 2))
done

BATT_WAIT=0
while [ "$BATT_WAIT" -lt 20 ]; do
    _v=$(cat /sys/class/power_supply/battery/temp 2>/dev/null)
    case "${_v:-0}" in
        [1-9]*|[-][1-9]*) break ;;
    esac
    sleep 2
    BATT_WAIT=$((BATT_WAIT + 2))
done
unset _v

_log "系统就绪 (boot=${WAIT}s sysfs=${SYSFS_WAIT}s batt=${BATT_WAIT}s)"

CDMSFILE="$CFGDIR/cdms.txt"
(
    sleep 10
    if [ -f "$CDMSFILE" ] && [ "$(tr -d '[:space:]' < "$CDMSFILE" 2>/dev/null)" = "1" ]; then
        _log "cdms.txt=1，触发重置"
        printf '0' > "$CDMSFILE"
        sleep 1
        printf '1' > "$CDMSFILE"
        _log "cdms.txt 重触发完成"
    fi
) &

is_alive() {
    _proc_alive "$PIDFILE" AaTempSpoof
}

cb_alive() {
    _proc_alive "$CBPID" cb
}

touch_alive() {
    _proc_alive "$TOUCHPID" touch_daemon
}

tombstone_alive() {
    _proc_alive "$TOMBSTONEPID" color_tombstone
}

charge_alive() {
    _pid_alive "$CHARGEPID"
}

start_daemon() {
    local i p try
    [ -x "$BIN" ] || { _log "AaTempSpoof 二进制不存在或不可执行"; return 1; }
    is_alive && { p=$(_pid_value "$PIDFILE"); _log "AaTempSpoof 已运行 PID=$p"; return 0; }

    rm -f "$PIDFILE"
    for try in 1 2 3; do
        _log "启动 AaTempSpoof (${try}/3)"
        "$BIN" --start --moddir "$MODDIR" >/dev/null 2>&1
        i=0
        while [ "$i" -lt 10 ]; do
            if is_alive; then
                touch "$FLAGFILE"
                p=$(_pid_value "$PIDFILE")
                _log "AaTempSpoof PID=$p"
                return 0
            fi
            sleep 1
            i=$((i + 1))
        done
        _log "AaTempSpoof 第 ${try} 次启动后未确认到进程"
        sleep 2
    done
    _log "⚠ AaTempSpoof 未检测到"
    return 1
}

_start_bg() {
    local bin="$1"
    local name="$2"
    local pidfile="$3"
    local label="$4"
    local p try i

    [ -x "$bin" ] || { _log "⚠ $label 不存在或不可执行，跳过"; return 0; }
    _proc_alive "$pidfile" "$name" && { p=$(_pid_value "$pidfile"); _log "$label 已运行 PID=$p"; return 0; }

    rm -f "$pidfile"
    for try in 1 2 3; do
        _log "启动 $label (${try}/3)"
        "$bin" >/dev/null 2>&1 &
        p=$!
        i=0
        while [ "$i" -lt 8 ]; do
            if _proc_alive "$pidfile" "$name"; then
                p=$(_pid_value "$pidfile")
                _log "$label PID=$p"
                return 0
            fi
            if [ "$i" -ge 2 ] && [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then
                printf '%s\n' "$p" > "$pidfile" 2>/dev/null
                _log "$label PID=$p (shell)"
                return 0
            fi
            sleep 1
            i=$((i + 1))
        done
        _log "$label 第 ${try} 次启动后未确认到进程"
        sleep 2
    done
    _log "⚠ $label 未检测到"
    return 1
}

start_cb() {
    _start_bg "$CB" cb "$CBPID" cb
}

start_touch() {
    _start_bg "$TOUCHBIN" touch_daemon "$TOUCHPID" touch_daemon
}

start_tombstone() {
    _start_bg "$TOMBSTONEBIN" color_tombstone "$TOMBSTONEPID" color_tombstone
}

start_charge() {
    local p try i
    [ -x "$CHARGEBIN" ] || { _log "⚠ charge_horae_daemon 不存在或不可执行，跳过"; return 0; }
    charge_alive && { p=$(_pid_value "$CHARGEPID"); _log "charge_horae_daemon 已运行 PID=$p"; return 0; }

    rm -f "$CHARGEPID"
    for try in 1 2 3; do
        _log "启动 charge_horae_daemon (${try}/3)"
        "$CHARGEBIN" >/dev/null 2>&1 &
        p=$!
        i=0
        while [ "$i" -lt 8 ]; do
            if charge_alive; then
                p=$(_pid_value "$CHARGEPID")
                _log "charge_horae_daemon PID=$p"
                return 0
            fi
            if [ "$i" -ge 2 ] && [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then
                printf '%s\n' "$p" > "$CHARGEPID" 2>/dev/null
                _log "charge_horae_daemon PID=$p (shell)"
                return 0
            fi
            sleep 1
            i=$((i + 1))
        done
        _log "charge_horae_daemon 第 ${try} 次启动后未确认到进程"
        sleep 2
    done
    _log "⚠ charge_horae_daemon 未检测到"
    return 1
}

ensure_all() {
    local round ok
    _log "$1：检查所有二进制"
    for round in 1 2 3; do
        ok=1
        is_alive        || { ok=0; start_daemon; }
        cb_alive        || { ok=0; start_cb; }
        touch_alive     || { ok=0; start_touch; }
        tombstone_alive || { ok=0; start_tombstone; }
        charge_alive    || { ok=0; start_charge; }
        [ "$ok" = "1" ] && { _log "$1：所有二进制已确认"; return 0; }
        sleep 3
    done
    _log "$1：仍有二进制未确认，等待后续 watchdog"
    return 1
}

_sec "守护进程启动"
ensure_all "首次启动"

(sleep 20
 [ -f "$STOP_FLAG" ] && exit 0
 ensure_all "20s 二次确认"
 if [ -s "$PIDFILE" ]; then
     read _p < "$PIDFILE"
     kill -HUP "$_p" 2>/dev/null
     _log "20s SIGHUP 已发送 (PID=$_p)"
 fi
) &

(sleep 60
 [ -f "$STOP_FLAG" ] && exit 0
 _log "60s 完整重初始化开始"
 "$BIN" --stop --moddir "$MODDIR" >/dev/null 2>&1
 sleep 2
 start_daemon
 ensure_all "60s 重初始化确认"
) &

if [ -f "$PIDDIR/hw" ]; then
    _log "检测到 hw 标记，禁用 HW 叠加层"
    service call SurfaceFlinger 1008 i32 1 >/dev/null 2>&1
fi

_sec "启动完成"

if [ -f "$NOTIFY_FILE" ]; then
    (
        _log "开始发送初始化通知"
        notify "$MODULE_NAME" "模块初始化加载中，预计15s加载完成"
        sleep 15
        notify "$MODULE_NAME" "模块加载完成，温控伪装挂载等已生效"
    ) &
else
    _log "通知文件不存在，跳过通知发送"
fi

renice 10 $$ >/dev/null 2>&1

FAIL=0

while true; do
    sleep 120

    [ -f "$STOP_FLAG" ] && continue

    if ! is_alive; then
        FAIL=$((FAIL + 1))
        _log "⚠ AaTempSpoof 死亡 ($FAIL/5)"
        rm -f "$PIDFILE"
        start_daemon && FAIL=0
    else
        FAIL=0
    fi

    if ! cb_alive; then
        _log "⚠ cb 死亡"
        rm -f "$CBPID"
        start_cb
    fi

    if ! touch_alive; then
        _log "⚠ touch_daemon 死亡"
        rm -f "$TOUCHPID"
        start_touch
    fi

    if ! tombstone_alive; then
        _log "⚠ color_tombstone 死亡"
        rm -f "$TOMBSTONEPID"
        start_tombstone
    fi

    if ! charge_alive; then
        _log "⚠ charge_horae_daemon 死亡"
        rm -f "$CHARGEPID"
        start_charge
    fi

    if [ "$FAIL" -gt 5 ]; then
        _log "✗ AaTempSpoof 连续重启失败，延长等待后继续监控"
        sleep 300
        FAIL=0
    fi
done
