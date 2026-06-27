#!/system/bin/sh

MODDIR=${0%/*}
MODULE_DIR="/data/adb/modules/AAaTempSpoof"
case "$MODDIR" in ""|".") MODDIR="$MODULE_DIR" ;; esac

DATA_DIR="$MODULE_DIR/AaTempSpoof"
PID_DIR="$MODULE_DIR/pid"
BATT_SYS="/sys/class/power_supply/battery"
BIN="$MODULE_DIR/bin/AaTempSpoof"

[ -d "$MODDIR/AaTempSpoof" ] && DATA_DIR="$MODDIR/AaTempSpoof"
[ -d "$MODDIR/pid" ] && PID_DIR="$MODDIR/pid"
[ -x "$MODDIR/bin/AaTempSpoof" ] && BIN="$MODDIR/bin/AaTempSpoof"

_write() {
    [ -w "$1" ] && printf '%s\n' "$2" > "$1" 2>/dev/null
}

_chmod_rw() {
    [ -e "$1" ] && chmod 0644 "$1" 2>/dev/null
}

_umount_all() {
    local I
    [ -n "$1" ] || return
    I=0
    while [ "$I" -lt 8 ]; do
        umount -l "$1" >/dev/null 2>&1 || break
        I=$((I + 1))
    done
}

_setprop() {
    setprop "$1" "$2" 2>/dev/null
    command -v resetprop >/dev/null 2>&1 && resetprop "$1" "$2" >/dev/null 2>&1
}

_delprop() {
    command -v resetprop >/dev/null 2>&1 || return
    resetprop -p -d "$1" >/dev/null 2>&1
    resetprop -d "$1" >/dev/null 2>&1
}

_kill_pid() {
    local PID
    PID=$(cat "$1" 2>/dev/null | tr -d ' \r\n')
    case "$PID" in ''|*[!0-9]*) return ;; esac
    [ "$PID" = "$$" ] && return
    kill "$PID" 2>/dev/null
    sleep 0.2
    kill -0 "$PID" 2>/dev/null && kill -9 "$PID" 2>/dev/null
}

_kill_exact() {
    local P
    for P in $(pgrep -x "$1" 2>/dev/null); do
        [ "$P" = "$$" ] || kill "$P" 2>/dev/null
    done
}

_kill_pattern() {
    local P
    for P in $(pgrep -f "$1" 2>/dev/null); do
        [ "$P" = "$$" ] || kill "$P" 2>/dev/null
    done
}

for B in "$BIN" "$MODDIR/bin/AaTempSpoof" "$MODULE_DIR/bin/AaTempSpoof"; do
    [ -x "$B" ] || continue
    "$B" --stop --moddir "$MODDIR" >/dev/null 2>&1
    [ "$MODDIR" = "$MODULE_DIR" ] || "$B" --stop --moddir "$MODULE_DIR" >/dev/null 2>&1
done

for PIDFILE in "$PID_DIR"/*.pid "$MODDIR/pid"/*.pid "$DATA_DIR"/*.pid \
               "$MODDIR/daemon.pid" "$DATA_DIR/daemon.pid"; do
    [ -f "$PIDFILE" ] && _kill_pid "$PIDFILE"
done

sleep 0.3
for PAT in "$MODULE_DIR/service.sh" "$MODDIR/service.sh"; do
    _kill_pattern "$PAT"
done
for NAME in AaTempSpoof cb touch_daemon touch_spoof_daemon color_tombstone charge_horae_daemon; do
    _kill_exact "$NAME"
done
sleep 0.3
for NAME in AaTempSpoof cb touch_daemon touch_spoof_daemon color_tombstone charge_horae_daemon; do
    for P in $(pgrep -x "$NAME" 2>/dev/null); do
        [ "$P" = "$$" ] || kill -9 "$P" 2>/dev/null
    done
done

for NODE in \
    "$BATT_SYS/temp" \
    "$BATT_SYS/uevent" \
    "$BATT_SYS/capacity" \
    "/sys/class/oplus_chg/battery/temp" \
    "/sys/class/oplus_chg/battery/batt_temp" \
    "/sys/class/oplus_chg/battery/temp_level" \
    "/sys/devices/platform/soc/soc:oplus,mms_gauge/oplus_mms/gauge/battery/temp" \
    "/sys/class/xm_power/fg_master/temp" \
    "/sys/devices/virtual/xm_power/fg_master/temp" \
    "/sys/class/oplus_chg/battery/bcc_parms" \
    "/sys/class/oplus_chg/battery/chip_soc"
do
    _umount_all "$NODE"
done

for NODE in \
    "$BATT_SYS/cycle_count" \
    "$BATT_SYS/battery_cycle" \
    "$BATT_SYS/batt_cycle" \
    "$BATT_SYS/cycle" \
    "/sys/class/power_supply/bms/cycle_count" \
    "/sys/class/power_supply/bms/battery_cycle" \
    "/sys/class/oplus_chg/battery/cycle_count" \
    "/sys/class/oplus_chg/battery/charge_cycle" \
    "/sys/class/oplus_chg/battery/batt_chargecycles" \
    "/sys/class/oplus_chg/battery/battery_cc" \
    "/sys/devices/platform/soc/soc:oplus,mms_gauge/oplus_mms/gauge/battery/cycle_count" \
    "/sys/class/qcom-battery/fake_cycle" \
    "/sys/class/xm_power/fg_master/qmax_cyclecount" \
    "/sys/class/xm_power/fg_master/cyclecount" \
    "/sys/devices/virtual/xm_power/fg_master/qmax_cyclecount" \
    "/sys/devices/virtual/xm_power/fg_master/cyclecount"
do
    _umount_all "$NODE"
done

for F in \
    "/vendor/etc/thermal-engine.conf" \
    "/vendor/etc/thermal-engine-v2.conf" \
    "/vendor/etc/thermal-engine.v3.conf" \
    "/vendor/etc/thermal-engine.v4.conf" \
    "/vendor/etc/thermal-engine.v5.conf" \
    "/vendor/etc/init/android.hardware.thermal-service.qti.rc" \
    "/vendor/etc/init/init_thermal-engine-v2.rc" \
    "/odm/etc/ThermalServiceConfig/sys_thermal_config.xml" \
    "/odm/etc/temperature_profile/sys_thermal_control_config.xml" \
    "/odm/etc/temperature_profile/sys_thermal_control_config_ext.xml" \
    "/odm/etc/temperature_profile/sys_high_temp_protect_OPPO_24811.xml" \
    "/odm/etc/temperature_profile/sys_thermal_zoom_window_restrict_list.xml" \
    "/odm/etc/horae/horae_target.conf" \
    "/system/vendor/etc/power_app_cfg.xml" \
    "/system/vendor/etc/powercontable.xml" \
    "/system/vendor/etc/powerscntbl.xml" \
    "/system/vendor/etc/.tp/.ht120.mtc" \
    "/system/vendor/etc/.tp/thermal.conf" \
    "/system/vendor/etc/.tp/thermal.off.conf" \
    "/system/vendor/etc/.tp/.thermal_policy_08" \
    "/odm/etc/powerhal/power_app_cfg.xml" \
    "/odm/etc/powerhal/powercontable.xml" \
    "/odm/etc/powerhal/powerscntbl.xml"
do
    _umount_all "$F"
done

if [ -r /proc/self/mountinfo ]; then
    awk -v mod="$MODULE_DIR" '$0 ~ mod {print $5}' /proc/self/mountinfo 2>/dev/null |
    sort -r |
    while IFS= read -r MP; do
        [ -n "$MP" ] && _umount_all "$MP"
    done
fi

for TZ in /sys/class/thermal/thermal_zone*; do
    _write "$TZ/emul_temp" 0
    _write "$TZ/mode" enabled
done
for CS in /sys/class/thermal/cooling_device*/cur_state; do
    _write "$CS" 0
done

for POLICY in /sys/devices/system/cpu/cpufreq/policy*; do
    MAX="$POLICY/cpuinfo_max_freq"
    SCA="$POLICY/scaling_max_freq"
    [ -r "$MAX" ] && [ -w "$SCA" ] && cat "$MAX" > "$SCA" 2>/dev/null
done

_chmod_rw "/sys/kernel/msm_performance/parameters/cpu_max_freq"
_chmod_rw "/sys/module/cpufreq_bouncing/parameters/enable"
_write "/sys/module/cpufreq_bouncing/parameters/enable" 1

_write "/sys/module/msm_thermal/parameters/enabled" 1
_write "/sys/module/msm_thermal/core_control/enabled" 1
_write "/sys/kernel/msm_thermal/enabled" 1
_write "/proc/oplus_temp/oplus_thermal_enable" 1
_write "/proc/oplus_temp/disable_thermal" 0
_write "/sys/class/thermal/thermal_message/board_sensor_temp_ratio" 100
_write "/sys/devices/platform/soc/soc:qcom,bcl-monitor/mode" enable
_write "/sys/devices/platform/soc/soc:qcom,bcl/mode" enable
_write "/proc/game_opt/disable_cpufreq_limit" 0

for N in \
    "/sys/class/xm_power/charger/charger_thermal/wired_thermal_remove" \
    "/sys/class/xm_power/charger/charger_thermal/wireless_thermal_remove" \
    "/sys/class/qcom-battery/thermal_remove"
do
    _write "$N" 0
done

for VA in FCC ICL WIRED_CURR_CTRL GAUGE_UPDATE \
          CHG_DISABLE CHG_SUSPEND WIRED_CHARGE_DONE COOL_DOWN \
          LOW_SOC TRICKLE_CHG LOW_CAPACITY PRE_CHG LOW_POWER; do
    _write "/proc/oplus-votable/$VA/force_active" 0
done
for VA in FCC ICL WIRED_CURR_CTRL GAUGE_UPDATE; do
    _write "/proc/oplus-votable/$VA/force_val" 0
done

_write "$BATT_SYS/siop_level" 100
for N in \
    "/sys/class/oplus_chg/battery/input_suspend" \
    "/sys/class/power_supply/battery/input_suspend" \
    "/sys/class/oplus_chg/battery/stop_chg" \
    "/sys/class/oplus_chg/common/chg_disable_votable" \
    "/sys/class/power_supply/battery/restricted_charging" \
    "/sys/class/qcom-battery/restrict_chg" \
    "/sys/devices/platform/soc/soc:google,charger/charge_disable" \
    "/sys/class/oplus_chg/battery/cool_down" \
    "/sys/class/oplus_chg/battery/chg_cool_down" \
    "/sys/class/oplus_chg/battery/thermal_ctrl" \
    "/sys/kernel/oplus_chg/battery/thermal_ctrl" \
    "/sys/class/oplus_chg/battery/super_endurance_mode" \
    "/sys/class/oplus_chg/battery/chg_cycle_enable" \
    "/sys/class/oplus_chg/common/chg_cycle_status" \
    "/sys/class/oplus_chg/battery/aging_fcc_voted" \
    "/sys/class/oplus_chg/battery/chg_protect_enable" \
    "/sys/class/oplus_chg/battery/battery_notify_code" \
    "/sys/class/oplus_chg/common/cool_down" \
    "/sys/class/oplus_chg/battery/slow_chg_enable" \
    "/sys/class/oplus_chg/common/slow_chg_enable" \
    "/sys/class/oplus_chg/common/pps_disable_votable" \
    "/sys/class/oplus_chg/common/ufcs_disable_votable" \
    "/sys/class/oplus_chg/common/pd_disable_votable" \
    "/sys/class/oplus_chg/battery/screen_off_chg_enable" \
    "/sys/class/oplus_chg/battery/screen_off_slow_chg" \
    "/sys/class/oplus_chg/common/screen_off_chg" \
    "/sys/class/power_supply/battery/screen_off_chg" \
    "/sys/devices/platform/charger/screen_off_throttle" \
    "/sys/class/oplus_chg/battery/night_chg_enable" \
    "/sys/class/oplus_chg/common/night_chg_enable" \
    "/sys/class/power_supply/battery/batt_slate_mode" \
    "/sys/class/power_supply/battery/store_mode" \
    "/sys/class/power_supply/battery/batt_misc_event" \
    "/sys/class/power_supply/battery/mmi_chrg_dis" \
    "/sys/class/power_supply/battery/smart_charging_activation_enabled" \
    "/sys/class/power_supply/battery/charge_rate" \
    "/sys/class/power_supply/battery/charge_control_limit" \
    "/sys/class/power_supply/battery/chg_control_limit_max" \
    "/sys/class/power_supply/battery/lim_charge" \
    "/sys/class/oplus_chg/battery/low_soc_limit" \
    "/sys/class/oplus_chg/common/low_soc_limit" \
    "/sys/kernel/oplus_chg/battery/low_chg_current" \
    "/sys/class/power_supply/battery/bd_trickle_cnt" \
    "/sys/class/power_supply/battery/bd_trickle_eoc" \
    "/sys/devices/platform/vivo_battery/screen_off_charge" \
    "/sys/devices/platform/vivo_battery/charge_sleep_mode" \
    "/sys/class/hw_power/charger/charge_data/iin_thermal_aux" \
    "/sys/class/mi_battchg/mi_battchg/thermal_level"
do
    _write "$N" 0
done

for N in \
    "/sys/class/oplus_chg/battery/mmi_charging_enable" \
    "/sys/class/oplus_chg/battery/battery_charging_enabled" \
    "/sys/class/power_supply/battery/battery_charging_enabled" \
    "/sys/class/power_supply/battery/charge_enabled" \
    "/sys/class/power_supply/battery/charging_enabled" \
    "/sys/class/hw_power/charger/charge_data/enable_charger" \
    "/sys/class/oplus_chg/battery/fast_chg_allow" \
    "/sys/class/oplus_chg/battery/pd_allow" \
    "/sys/class/oplus_chg/common/pd_allow" \
    "/sys/class/oplus_chg/battery/vooc_allow" \
    "/sys/class/oplus_chg/battery/wls_boost_en" \
    "/sys/class/oplus_chg/battery/wired_boost_enable" \
    "/sys/class/oplus_chg/common/wired_boost_enable" \
    "/sys/kernel/oplus_chg/battery/voocphy_enable" \
    "/sys/class/oplus_chg/battery/thermal_enable" \
    "/sys/class/power_supply/battery/thermal_feature_on" \
    "/sys/class/mi_battchg/mi_battchg/thermal_enable"
do
    _write "$N" 1
done

CUP="/sys/class/oplus_chg/common/chg_up_limit"
[ -e "$CUP" ] && chmod 0644 "$CUP" 2>/dev/null

if [ -w "/proc/shell-temp" ]; then
    I=0
    while [ "$I" -le 9 ]; do
        printf '%d 0\n' "$I" > "/proc/shell-temp" 2>/dev/null
        I=$((I + 1))
    done
fi

for P in \
    persist.vendor.charge.thermal.control \
    persist.vendor.oplus.charge.cooldown \
    persist.vendor.oplus.slow_chg_enable \
    persist.oplus.chg.vooc.allow \
    persist.oplus.chg.ufcs.allow \
    persist.oplus.chg.pps.allow \
    persist.oplus.chg.voocphy.allow \
    persist.oplus.chg.svooc.allow \
    persist.oplus.chg.flash_charge.allow \
    persist.oplus.chg.cool_down \
    persist.oplus.chg.screenoff.decharge \
    persist.oplus.chg.screen_off_decharge \
    persist.oplus.chg.screen_off_slow_chg \
    persist.oplus.chg.night_charging \
    persist.sys.powerhal.thermal.disabled \
    persist.sys.charge.restrict \
    persist.sys.charge.screenoff \
    persist.sys.charge.sleep_mode \
    persist.chg.screen_off_reduce \
    persist.vendor.chg.screen_off_reduce \
    persist.vendor.battery.sleep_charging \
    persist.vendor.dgb.sleep.chg.enabled \
    persist.hw.charge.screenoff \
    persist.vivo.charge.screenoff \
    persist.vivo.charger.sleep_mode \
    persist.mmi.charge.screenoff \
    persist.samsung.charge.screenoff \
    persist.miui.charge.screenoff \
    persist.sys.miui_charge_screen_off \
    persist.sys.oplus.wifi.sla.game_high_temperature \
    persist.sys.environment.temp \
    persist.sys.ui.hw \
    ro.oplus.charge.thermal.limit \
    ro.oplus.audio.thermal_control \
    ro.oplus.radio.hide_nr_switch \
    oplus.dex.tempcontrol \
    dalvik.vm.dexopt.thermal-cutoff \
    gputuner_switch \
    sys.oppo.high.performance
do
    _delprop "$P"
done

_setprop sys.thermal.enable true
_setprop sys.enable.hypnus 1
_setprop persist.vendor.enable.cpulimit true
_setprop persist.sys.powerhal.thermal.disabled 0
_setprop vendor.battery.charge.disable 0
_setprop vendor.battery.restrict.charging 0
_setprop vendor.battery.screenoff.decharge 0
_setprop sys.oppo.high.performance 0
_setprop gputuner_switch false
_setprop oplus.dex.tempcontrol true
_setprop dalvik.vm.dexopt.thermal-cutoff 1
_setprop ro.oplus.charge.thermal.limit 1
_setprop ro.oplus.audio.thermal_control 1
if getprop persist.sys.oiface.enable >/dev/null 2>&1; then
    _setprop persist.sys.horae.enable 1
fi

dumpsys battery reset 2>/dev/null
service call SurfaceFlinger 1008 i32 0 >/dev/null 2>&1
_write "/sys/class/power_supply/usb/apsd_rerun" 1

for SVC in thermal-engine vendor.thermal-engine vendor.thermal_manager thermald \
           thermal_mnt_hal_service android.thermal-hal vendor.thermal-hal-2-0 \
           vendor.thermal-hal-2-1 oppo_theias orms-hal-1-0 \
           vendor.oplus.ormsHalService-aidl-default vendor.oplus.ormsHalService-aidl-defaults \
           fuelgauged smartcharging horae
do
    start "$SVC" 2>/dev/null
done

chattr -i /data/vendor/thermal /data/vendor/thermal/config \
          /data/vendor/thermal/config/* 2>/dev/null
rm -rf /data/vendor/thermal/config/* 2>/dev/null
rm -rf /data/thermal/config/* 2>/dev/null

if [ -f /data/system/refresh_rate_config.xml.bak ]; then
    cp -fp /data/system/refresh_rate_config.xml.bak /data/system/refresh_rate_config.xml 2>/dev/null
    rm -f /data/system/refresh_rate_config.xml.bak 2>/dev/null
fi

rm -f "$MODDIR/daemon.pid" "$MODDIR/fake_batt_temp" "$MODDIR/user_stopped" \
      "$MODDIR/platform.txt" "$MODDIR/tempspoof.log" 2>/dev/null

rm -f "$DATA_DIR/daemon.pid" "$DATA_DIR/fake_batt_temp" "$DATA_DIR/fake_uevent" \
      "$DATA_DIR/fake_bcc_parms" "$DATA_DIR/fake_cycle_count" "$DATA_DIR/fake_capacity" \
      "$DATA_DIR/cb.pid" "$DATA_DIR/plug.log" "$DATA_DIR/plug.log.bak" \
      "$DATA_DIR/touch_daemon.log" "$DATA_DIR/touch_daemon.log.bak" \
      "$DATA_DIR/charge_horae_daemon.log" "$DATA_DIR/charge_horae_daemon.log.bak" \
      "$DATA_DIR/color_tombstone.log" "$DATA_DIR/color_tombstone.log.bak" 2>/dev/null

rm -rf "$PID_DIR" "$MODDIR/pid" 2>/dev/null
rm -f "$MODULE_DIR/webroot/首次通知.css" "$MODULE_DIR/webroot/通知.png" 2>/dev/null

pm path com.aatempspoof.tile >/dev/null 2>&1 && pm uninstall com.aatempspoof.tile >/dev/null 2>&1

command -v ksud >/dev/null 2>&1 && ksud module uninstall AAaTempSpoof >/dev/null 2>&1
_umount_all "$MODULE_DIR"
[ -d "$MODULE_DIR" ] && rm -rf "$MODULE_DIR" 2>/dev/null

exit 0
