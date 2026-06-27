#!/system/bin/sh
SKIPUNZIP=1
_VOL_DEV=""
for _d in /dev/input/event*; do
    getevent -p "$_d" 2>/dev/null | grep -q "KEY_VOLUMEUP" && _VOL_DEV="$_d" && break
done

wait_volume_key() {
    VOLKEY_RESULT="up"
    while true; do
        if [ -n "$_VOL_DEV" ]; then
            _ev=$(getevent -lqc 1 "$_VOL_DEV" 2>/dev/null)
        else
            _ev=$(getevent -lqc 1 2>/dev/null)
        fi
        case "$_ev" in
            *KEY_VOLUMEUP*DOWN*)   VOLKEY_RESULT="up";   return ;;
            *KEY_VOLUMEDOWN*DOWN*) VOLKEY_RESULT="down"; return ;;
        esac
    done
}

IS_JAILBREAK=0

ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  选择模块工作模式"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  【音量+】高级模式"
ui_print "  │          可通过 WebUI 自定义调节伪装参数"
ui_print "  │  【音量-】极简模式"
ui_print "  │          懒人小白专属，无webui，刷入即用"
ui_print "  │          无需任何配置，自动解放充电/性能"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "up" ]; then
    WORK_MODE="advanced"
    ui_print "  ➜ 高级模式，WebUI 可自定义配置"
else
    WORK_MODE="simple"
    ui_print "  ➜ 简易模式，刷入即用"
fi
ui_print " "

if [ "$WORK_MODE" = "advanced" ]; then

ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  当前设备根状态是否为免解越狱？"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  【音量+】是"
ui_print "  │  【音量-】否"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key

if [ "$VOLKEY_RESULT" = "up" ]; then
    IS_JAILBREAK=1
    ui_print "================================"
    ui_print "  ⚠ 免解临时 Root 设备（越狱模式）"
    ui_print "================================"
    ui_print "  当前机型为免解临时 Root 设备"
    ui_print "  更新模块请按以下步骤操作："
    ui_print "  1. 卸载本模块"
    ui_print "  2. 硬重启掉权限"
    ui_print "  3. 重新提权 Root"
    ui_print "  4. 再次刷入模块进行更新"
    ui_print "================================"
    ui_print " "
    ui_print "  ┌─────────────────────────────────────┐"
    ui_print "  │  【音量+】已了解风险，继续安装        │"
    ui_print "  │  【音量-】退出，按步骤操作后再安装    │"
    ui_print "  └─────────────────────────────────────┘"
    ui_print " "
    wait_volume_key
    if [ "$VOLKEY_RESULT" = "down" ]; then
        abort "安装已终止：请卸载模块硬重启后重新提权再安装"
    fi
    ui_print "  ➜ 继续以越狱模式安装"
else
    ui_print "  ➜ 永久 Root，正常模式安装"
fi

fi

unzip -o "$ZIPFILE" -d "$MODPATH" >&2 || abort "解压失败"
rm -rf "$MODPATH/META-INF"

module="$MODPATH"

sh /data/adb/modules/AaTempSpoof/uninstall.sh 2>/dev/null
rm -rf /data/adb/modules/AaTempSpoof
sh /data/adb/modules/AaTempdc/uninstall.sh 2>/dev/null
rm -rf /data/adb/modules/AaTempdc
rm -rf /data/adb/modules/AAaTempSpoof/Aa自定义配置.txt
set_perm "$MODPATH/bin/touch_daemon" 0 0 0755 u:object_r:system_file:s0

ui_print "================================"
ui_print "   AaTempSpoof  "
ui_print "   温度伪装 + 全机型去温控"
ui_print "   最大化充电速度版"
ui_print "================================"

BRAND=$(getprop ro.product.brand 2>/dev/null)
MODEL=$(getprop ro.product.model 2>/dev/null)
DEVICE=$(getprop ro.product.device 2>/dev/null)
ANDROID_VER=$(getprop ro.build.version.release 2>/dev/null)
SDK_VER=$(getprop ro.build.version.sdk 2>/dev/null)
ABI=$(getprop ro.product.cpu.abi 2>/dev/null)
SOC=$(getprop ro.board.platform 2>/dev/null)
SOC_MODEL=$(getprop ro.soc.model 2>/dev/null)
SOC_MANU=$(getprop ro.soc.manufacturer 2>/dev/null)
HARDWARE=$(getprop ro.hardware 2>/dev/null)
RAM_KB=$(awk '/MemTotal/{print $2}' /proc/meminfo 2>/dev/null)
RAM_GB=$(( (RAM_KB + 524288) / 1048576 ))

MARKET=$(getprop ro.vendor.oplus.market.name 2>/dev/null)
[ -z "$MARKET" ] && MARKET=$(getprop ro.product.marketname 2>/dev/null)
[ -z "$MARKET" ] && MARKET=$(getprop ro.product.market.name 2>/dev/null)
[ -z "$MARKET" ] && MARKET=$(getprop ro.oppo.market.name 2>/dev/null)
[ -z "$MARKET" ] && MARKET="$MODEL"

if [ -n "$SOC_MODEL" ] && [ -n "$SOC_MANU" ]; then
    SOC_DISPLAY="$SOC_MANU $SOC_MODEL"
elif [ -n "$SOC_MODEL" ]; then
    SOC_DISPLAY="$SOC_MODEL"
elif [ -n "$SOC" ]; then
    SOC_DISPLAY="$SOC"
else
    SOC_DISPLAY="未知"
fi

ui_print " "
ui_print "┌─────────────────────────────┐"
ui_print "│         设备信息             │"
ui_print "├─────────────────────────────┤"
ui_print "│ 品牌    : $BRAND"
ui_print "│ 名称    : $MARKET"
ui_print "│ 型号    : $MODEL"
ui_print "│ 代号    : $DEVICE"
ui_print "│ Android : $ANDROID_VER  (SDK $SDK_VER)"
ui_print "│ ABI     : $ABI"
ui_print "│ 处理器  : $SOC_DISPLAY"
ui_print "│ 平台    : $SOC / $HARDWARE"
ui_print "│ 内存    : 约 ${RAM_GB}GB"
ui_print "└─────────────────────────────┘"
ui_print " "

case "$ABI" in
    arm64*) ui_print "  ✓ arm64 架构兼容" ;;
    *)      abort "❌ 本模块仅支持 arm64 设备 (当前: $ABI)" ;;
esac

KERNEL_VER=$(uname -r 2>/dev/null)
KERNEL_MAJOR=$(printf '%s' "$KERNEL_VER" | sed 's/^\([0-9]*\).*/\1/')
KERNEL_MINOR=$(printf '%s' "$KERNEL_VER" | sed 's/^[0-9]*\.\([0-9]*\).*/\1/')
ui_print "  内核版本: $KERNEL_VER"
if [ "$KERNEL_MAJOR" -gt 5 ] || { [ "$KERNEL_MAJOR" -eq 5 ] && [ "$KERNEL_MINOR" -ge 10 ]; }; then
    ui_print "  ✓ 内核版本符合要求（>= 5.10）"
else
    abort "❌ 内核版本 $KERNEL_VER 不满足要求（需大于等于 5.10，当前处理器不支持）"
fi

icon_ok() { [ "$1" -gt 0 ] && printf "✓" || printf "✗"; }

DEVICE_LABEL="$MARKET"
if [ "$WORK_MODE" = "advanced" ]; then
ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  当前机型: $MARKET"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  选择模块控制台显示名称"
ui_print "  │  【音量+】新版: ${MARKET} 伪装控制台"
ui_print "  │  【音量-】经典: AaTempSpoof"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "up" ]; then
    CONSOLE_NAME="${MARKET} 伪装控制台"
    CONSOLE_DESC="为[${MARKET}]提供自定义节点伪装[emuLtemp+bind-mount双挂载]+热控XML配置挂载覆盖+禁用相关governor并清除cooling device"
    ui_print "  ➜ 新版名称: $CONSOLE_NAME"
else
    CONSOLE_NAME="AaTempSpoof"
    CONSOLE_DESC="自定义节点伪装[emuLtemp+bind-mount双挂载]+热控XML配置挂载覆盖+禁用相关governor并清除cooling device"
    ui_print "  ➜ 经典名称: AaTempSpoof"
fi
else
    CONSOLE_NAME="AaTempSpoof"
    CONSOLE_DESC="自定义节点伪装[emuLtemp+bind-mount双挂载]+热控XML配置挂载覆盖+禁用相关governor并清除cooling device"
fi

ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  当前设备: $BRAND $MODEL"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  模块全安卓适配"
ui_print "  │  正在自动识别品牌选择运行模式..."
ui_print "  └─────────────────────────────────────┘"
ui_print " "

BRAND_LOWER=$(printf '%s' "$BRAND" | tr '[:upper:]' '[:lower:]')
case "$BRAND_LOWER" in
    oneplus|oppo|realme)
        ui_print "  ➜ 完整模式，识别到欧加真设备: $BRAND"
        ui_print "  ✓ OnePlus 全功能已启用"
        mkdir -p "$MODPATH/pid"
        touch "$MODPATH/pid/充电禁用horae"
        mkdir -p "/data/adb/modules/AAaTempSpoof/pid"
        touch "/data/adb/modules/AAaTempSpoof/pid/充电禁用horae"
        ui_print "  ✓ 已创建灭屏充电提速标志"
        ;;
    *)
        mkdir -p "$MODPATH/pid"
        touch "$MODPATH/pid/others"
        mkdir -p "$MODPATH/AaTempSpoof"
        ui_print "  ➜ 兼容模式，非欧加真设备: $BRAND"
        ui_print "  ✓ OnePlus 专属功能已禁用"
        ;;
esac

ui_print " "
ui_print "  ── 温度伪装节点检测 ──"
N_CPU=0; N_GPU=0; N_MEM=0; N_BATT=0; N_PERIPH=0; N_MISC=0; N_TOTAL=0

for tz in /sys/class/thermal/thermal_zone*; do
    [ -w "$tz/emul_temp" ] || continue
    TYPE=$(cat "$tz/type" 2>/dev/null)
    t=$(printf '%s' "$TYPE" | tr '[:upper:]' '[:lower:]')

    case "$t" in
        *modem*|*mdm*|*q6*|*wlan*|*wcss*|*wifi*|\
        *rfa*|*rfc*|*rf-*|*xo-*|*ipa*|*rpm*|\
        *sub6*|*nr5g*|*antenna*|*qtm*|*mmw*|*nss*)
            continue ;;
    esac

    case "$t" in
        *cpuss*|*cluster*|*kryo*|*silver*|*gold*|*prime*|*cpu-*|*cpu0*|*cpufreq*)
            N_CPU=$(( N_CPU + 1 )) ;;
        *cpu*)
            case "$t" in *cpu_c*) ;; *) N_CPU=$(( N_CPU + 1 )) ;; esac ;;
        *gpu*|*kgsl*|*adreno*|*gpuss*|*gpufreq*|*mdla*|*apu*)
            N_GPU=$(( N_GPU + 1 )) ;;
        *ddr*|*dram*|*llcc*|*mem*|*npu*|*nsp*|*iommu*)
            N_MEM=$(( N_MEM + 1 )) ;;
        *batt*|*battery*|*bms*|*charger*|*usb-therm*|*vbat*|*ibat*)
            N_BATT=$(( N_BATT + 1 )) ;;
        *pm8*|*pa-therm*|*board*|*shell*|*skin*|*ap_ntc*|\
        *cam*|*rear*|*oplus_thermal*|*ntc*|*ambient*|*wcn*|*ltepa*|*nrpa*)
            N_PERIPH=$(( N_PERIPH + 1 )) ;;
        *xo*)
            N_PERIPH=$(( N_PERIPH + 1 )) ;;
        *)
            N_MISC=$(( N_MISC + 1 )) ;;
    esac
    N_TOTAL=$(( N_TOTAL + 1 ))
done

ui_print "  CPU  : $(icon_ok $N_CPU) ${N_CPU}个   GPU  : $(icon_ok $N_GPU) ${N_GPU}个"
ui_print "  内存 : $(icon_ok $N_MEM) ${N_MEM}个   电池 : $(icon_ok $N_BATT) ${N_BATT}个"
ui_print "  外围 : $(icon_ok $N_PERIPH) ${N_PERIPH}个   其他 : $(icon_ok $N_MISC) ${N_MISC}个"
ui_print "  可伪装节点总数: $N_TOTAL"

COMPAT_WARN=""
if [ "$N_TOTAL" -eq 0 ]; then
    COMPAT_WARN="未发现任何可写 emul_temp 节点，温度伪装将完全无效"
elif [ "$N_CPU" -eq 0 ] && [ "$N_GPU" -eq 0 ]; then
    COMPAT_WARN="CPU/GPU 温度节点均不可写，伪装效果受限"
else
    ui_print "  ✓ 节点检测正常，温度伪装可生效"
fi

ui_print " "
if [ "$WORK_MODE" = "advanced" ]; then
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  节点识别完成，是否继续安装？         │"
ui_print "  │  【音量+】继续安装                   │"
ui_print "  │  【音量-】退出，放弃安装              │"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "down" ]; then
    abort "用户取消安装"
fi
ui_print "  ➜ 继续安装"
ui_print " "

if [ -n "$COMPAT_WARN" ]; then
    ui_print " "
    ui_print "╔══════════════════════════════════╗"
    ui_print "║  ✗✗✗   兼容性警告   ✗✗✗         ║"
    ui_print "╠══════════════════════════════════╣"
    ui_print "║  $COMPAT_WARN"
    ui_print "╚══════════════════════════════════╝"
    ui_print " "
    ui_print "  ┌─────────────────────────────┐"
    ui_print "  │  【音量+】忽略警告，强制安装  │"
    ui_print "  │  【音量-】退出，放弃安装      │"
    ui_print "  └─────────────────────────────┘"
    ui_print " "
    wait_volume_key
    [ "$VOLKEY_RESULT" = "up" ] && ui_print "  ➜ 强制安装，风险自负" || abort "用户取消安装"
else
    ui_print "  ┌─────────────────────────────────────┐"
    ui_print "  │  ⚠ 与去温控/伪装/充电加速类模块严重冲突"
    ui_print "  │  若已刷入类似模块，请先卸载并重启"
    ui_print "  └─────────────────────────────────────┘"
    ui_print " "
    ui_print "  ┌─────────────────────────────────────┐"
    ui_print "  │  【音量+】已清理冲突模块或无冲突"
    ui_print "  │  【音量-】未清理，退出安装"
    ui_print "  └─────────────────────────────────────┘"
    ui_print " "
    wait_volume_key
    [ "$VOLKEY_RESULT" = "up" ] && ui_print "  ➜ 确认安装" || abort "用户取消安装"
fi
else
    ui_print "  ┌─────────────────────────────────────┐"
    ui_print "  │  ⚠ 与去温控/伪装/充电加速类模块严重冲突"
    ui_print "  │  若已刷入类似模块，请先卸载并重启"
    ui_print "  └─────────────────────────────────────┘"
    ui_print " "
    ui_print "  ┌─────────────────────────────────────┐"
    ui_print "  │  【音量+】已清理冲突模块或无冲突"
    ui_print "  │  【音量-】未清理，退出安装"
    ui_print "  └─────────────────────────────────────┘"
    ui_print " "
    wait_volume_key
    [ "$VOLKEY_RESULT" = "up" ] && ui_print "  ➜ 确认安装" || abort "用户取消安装"
fi

[ -f "$MODPATH/bin/AaTempSpoof" ] || abort "❌ 缺少二进制文件"
ui_print "  ✓ 二进制就绪"

if printf '%s' "$CONSOLE_NAME" | grep -q "伪装控制台" && [ "$IS_JAILBREAK" = "1" ]; then
    CONSOLE_NAME="${CONSOLE_NAME}(越狱模式)"
    ui_print "  ✓ 越狱模式，名称附加(越狱模式)"
fi
sed -i "s|^name=.*|name=$CONSOLE_NAME|" "$MODPATH/module.prop"
sed -i "s|^description=.*|description=$CONSOLE_DESC|" "$MODPATH/module.prop"
ui_print "  ✓ 模块名称已设置: $CONSOLE_NAME"

if [ "$WORK_MODE" = "simple" ]; then
    sed -i "s|^description=.*|description=(极简模式 CPU/GPU/DDR40°BAT30°)+去温控|" "$MODPATH/module.prop"
    rm -f "$MODPATH/action.sh"
    rm -rf "$MODPATH/webroot"
    ui_print "  ✓ 简易模式：已移除 WebUI 与 action.sh"
fi

dirs="/odm /my_product /my_stock /vendor /system/vendor /product /system"

xml_override() {
    local fname="$1" overrides="$2"
    for file in $(find $dirs -name "$fname" 2>/dev/null); do
        mkdir -p "$(dirname "$module$file")"
        rows=$(cat "$file")
        for override in $overrides; do
            key=$(printf '%s' "$override" | cut -f1 -d'=')
            value=$(printf '%s' "$override" | cut -f2 -d'=')
            rows=$(printf '%s' "$rows" | sed "s/<$key>.*</<$key>$value</")
        done
        printf '%s\n' "$rows" > "$module$file"
    done
}

ui_print "  正在应用全机型去温控..."

boolValues="feature_enable_item feature_safety_test_enable_item aging_thermal_control_enable_item"
intValues="aging_cpu_level_item high_temp_safety_level_item game_high_perf_mode_item normal_mode_item ota_mode_item racing_mode_item"
for file in $(find $dirs -name "sys_thermal_control_config*.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    rows=$(cat "$file" | grep -v -E '(<gear_config|cpu=|fps=|<scene_|</scene_|<category_|</category_|<subitem|<level|\.)')
    for key in $boolValues; do
        rows=$(printf '%s' "$rows" | sed "s/<$key.*\/>/<$key booleanVal=\"false\" \/>/" )
    done
    for key in $intValues; do
        rows=$(printf '%s' "$rows" | sed "s/<$key.*\/>/<$key intVal=\"-1\" \/>/" )
    done
    printf '%s\n' "$rows" | tr -s '\n' > "$module$file"
done

xml_override 'sys_thermal_config.xml' "isOpen=0
more_heat_threshold=550
heat_threshold=530
less_heat_threshold=500
preheat_threshold=480
preheat_dex_oat_threshold=460
thermal_battery_temp=0
is_feature_on=0
is_upload_log=0
is_upload_errlog=0"

xml_override 'sys_high_temp_protect*xml' "isOpen=0
HighTemperatureProtectSwitch=false
HighTemperatureShutdownSwitch=false
HighTemperatureFirstStepSwitch=false
HighTemperatureProtectFirstStepIn=600
HighTemperatureProtectFirstStepOut=580
HighTemperatureProtectThresholdIn=620
HighTemperatureProtectThresholdOut=600
HighTemperatureProtectShutDown=800
MediumTemperatureProtectThreshold=10000
HighTemperatureDisableFlashSwitch=false
HighTemperatureDisableFlashLimit=600
HighTemperatureEnableFlashLimit=580
HighTemperatureDisableFlashChargeSwitch=false
HighTemperatureDisableFlashChargeLimit=600
HighTemperatureEnableFlashChargeLimit=580
camera_temperature_limit=600
HighTemperatureControlVideoRecordSwitch=false
HighTemperatureDisableVideoRecordLimit=600
HighTemperatureEnableVideoRecordLimit=580
ToleranceThreshold=50
ToleranceStart=580
ToleranceStop=560"

for file in $(find $dirs -name "thermal_manager_config*.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    sed 's/enabled="true"/enabled="false"/g;
         s/enable="1"/enable="0"/g;
         s/<Algorithm.*\/>/<Algorithm enabled="false" \/>/g;
         s/throttleThreshold="[0-9]*"/throttleThreshold="999"/g;
         s/shutdownThreshold="[0-9]*"/shutdownThreshold="999"/g' \
        "$file" > "$module$file"
done

for file in $(find $dirs -name "thermal_policy*.xml" -o -name "thermal_sensor_conf*.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    sed 's/<Trip.*type="passive".*\/>/<Trip temperature="99000" type="passive" hysteresis="0" \/>/g;
         s/<Trip.*type="hot".*\/>/<Trip temperature="99000" type="hot" hysteresis="0" \/>/g;
         s/<Trip.*type="critical".*\/>/<Trip temperature="125000" type="critical" hysteresis="0" \/>/g' \
        "$file" > "$module$file"
done

for file in $(find $dirs -name "thermal*.conf" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    sed 's/^set_point[[:space:]]*[0-9]*/set_point 99000/g;
         s/^set_point_clr[[:space:]]*[0-9]*/set_point_clr 95000/g' \
        "$file" > "$module$file"
done

for file in $(find $dirs -name "oplus_charge*.xml" -o -name "charge_temp*.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    sed 's/batt_temp_high="[0-9]*"/batt_temp_high="700"/g;
         s/batt_temp_low="[0-9]*"/batt_temp_low="-200"/g;
         s/batt_temp_exit="[0-9]*"/batt_temp_exit="680"/g;
         s/skin_temp_high="[0-9]*"/skin_temp_high="700"/g;
         s/led_temp_high="[0-9]*"/led_temp_high="700"/g' \
        "$file" > "$module$file"
done

_screen_off_patch() {
    sed -i 's/screen_off_enable="[01]"/screen_off_enable="0"/g;
            s/night_charge_enable="[01]"/night_charge_enable="0"/g;
            s/cool_down_enable="[01]"/cool_down_enable="0"/g;
            s/slow_charge_enable="[01]"/slow_charge_enable="0"/g;
            s/sleep_mode_enable="[01]"/sleep_mode_enable="0"/g;
            s/restrict_charge_enable="[01]"/restrict_charge_enable="0"/g;
            s/screenoff_current="[0-9]*"/screenoff_current="99999"/g;
            s/screen_off_current_limit="[0-9]*"/screen_off_current_limit="99999"/g;
            s/night_current="[0-9]*"/night_current="99999"/g;
            s/sleep_current="[0-9]*"/sleep_current="99999"/g;
            s/restrict_current="[0-9]*"/restrict_current="99999"/g;
            s/screen_off_icl="[0-9]*"/screen_off_icl="99999"/g;
            s/screen_off_fcc="[0-9]*"/screen_off_fcc="99999"/g' "$1"
}

for file in $(find $dirs \
    -name "oplus_chg_config*.xml" \
    -o -name "screen_off_charge*.xml" \
    -o -name "charging_screen_off*.xml" \
    -o -name "charge_screen_off*.xml" \
    -o -name "miui_charge_config*.xml" \
    -o -name "battery_charge_config*.xml" \
    -o -name "charge_limit_config*.xml" \
    -o -name "vivo_charge_config*.xml" \
    -o -name "charging_config*.xml" \
    -o -name "moto_charge_config*.xml" \
    -o -name "samsung_charge_config*.xml" \
    -o -name "huawei_charge_config*.xml" \
    -o -name "hw_charge_config*.xml" \
    2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    cp -f "$file" "$module$file"
    _screen_off_patch "$module$file"
done

for file in $(find $dirs -name "refresh_rate_config.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    cp -fp "$file" "$module$file"
    sed -i 's/<!--.*-->//' "$module$file"
    sed -i '/<item.*2-2-2-2.*\/>/d' "$module$file"
    sed -i 's/2-2-2-2/0-0-0-0/' "$module$file"
    sed -i '/<record/d' "$module$file"
done
rr_config=/data/system/refresh_rate_config.xml
if [ -f "$rr_config" ]; then
    cp -f "$rr_config" "${rr_config}.bak"
    sed -i 's/<!--.*-->//' "$rr_config"
    sed -i '/<item.*2-2-2-2.*\/>/d' "$rr_config"
    sed -i 's/2-2-2-2/0-0-0-0/' "$rr_config"
    sed -i '/<record/d' "$rr_config"
fi

for file in $(find $dirs -name "thermallevel_to_fps.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    cp -fp "$file" "$module$file"
    sed -i "s/fps=\".*\"/fps=\"144\"/" "$module$file"
done

for file in $(find $dirs -name "game_thermal_config.xml" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    if grep -q 'cluster3' "$file" 2>/dev/null; then
        cat > "$module$file" << 'XMLEOF'
<?xml version="1.0" encoding="utf-8"?>
<game_thermal_config>
    <version>20230829</version>
    <filter-name>game_thermal_config</filter-name>
    <heavy_policy>
        <game_control temp="600" cluster0="-1" cluster1="-1" cluster2="-1" cluster3="-1" fps="0"/>
    </heavy_policy>
    <default_policy>
        <game_control temp="500" cluster0="-1" cluster1="-1" cluster2="-1" cluster3="-1" fps="0"/>
        <game_control temp="550" cluster0="-1" cluster1="-1" cluster2="-1" cluster3="-1" fps="0"/>
        <game_control temp="600" cluster0="-1" cluster1="-1" cluster2="-1" cluster3="-1" fps="0"/>
    </default_policy>
</game_thermal_config>
XMLEOF
    else
        cat > "$module$file" << 'XMLEOF'
<?xml version="1.0" encoding="utf-8"?>
<game_thermal_config>
    <version>20230829</version>
    <filter-name>game_thermal_config</filter-name>
    <heavy_policy>
        <game_control temp="600" cluster0="-1" cluster1="-1" cluster2="-1" fps="0"/>
    </heavy_policy>
    <default_policy>
        <game_control temp="500" cluster0="-1" cluster1="-1" cluster2="-1" fps="0"/>
        <game_control temp="550" cluster0="-1" cluster1="-1" cluster2="-1" fps="0"/>
        <game_control temp="600" cluster0="-1" cluster1="-1" cluster2="-1" fps="0"/>
    </default_policy>
</game_thermal_config>
XMLEOF
    fi
done

for file in $(find $dirs -name "QEGA_Config.txt" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    printf 'SkinTemperatureNode:   battery\nSkinNodeThrottleTemp:  65000\n#GameID   GameAPK    MaxTemperature  MaxCurrent  AvgCurrent\n100001    hok         65000          6000        5000\n0         adaptive    65000          6000        5000\n' > "$module$file"
done

for file in $(find $dirs -name "devices_config.json" 2>/dev/null); do
    local dst="$module$file"
    mkdir -p "$(dirname "$dst")"
    : > "$dst"
    while IFS= read -r line; do
        case "$line" in
            *'"battery.temperate.range":'*)
                printf '"battery.temperate.range": "[-100,700]",\n' >> "$dst" ;;
            *'"high.capacity.battery.temperate.range":'*)
                printf '"high.capacity.battery.temperate.range": "[-100,700]",\n' >> "$dst" ;;
            *'"high.capacity.threshold":'*)
                printf '"high.capacity.threshold": 99\n' >> "$dst" ;;
            *)
                printf '%s\n' "$line" >> "$dst" ;;
        esac
    done < "$file"
done

ui_print "  ✓ 全机型去温控 XML 完成"

ui_print "  正在最大化充电速度..."

for file in $(find $dirs -name "charging_*txt" 2>/dev/null); do
    mkdir -p "$(dirname "$module$file")"
    : > "$module$file"
    while IFS= read -r line; do
        case "$line" in
            *:=*) printf '%s\n' "$line" >> "$module$file" ;;
            *,*,*)
                temp=$(printf '%s' "$line" | awk -F, '{print $1}')
                cur=$(printf '%s' "$line" | awk -F, '{print $2}')
                t=$(printf '%s' "$line" | awk -F, '{print $3}')
                printf '%s,%s,%s\n' "$(( temp + 100 ))" "$cur" "$t" >> "$module$file" ;;
            *) printf '%s\n' "$line" >> "$module$file" ;;
        esac
    done < "$file"
done

for n in \
    /sys/class/oplus_chg/battery/slow_chg_enable \
    /sys/class/oplus_chg/common/slow_chg_enable \
    /sys/class/power_supply/battery/thermal_feature_on \
    /sys/class/oplus_chg/battery/thermal_ctrl \
    /sys/class/oplus_chg/battery/super_endurance_mode \
    /sys/class/oplus_chg/battery/screen_off_chg_enable \
    /sys/class/oplus_chg/common/screen_off_chg \
    /sys/class/oplus_chg/battery/screen_off_slow_chg \
    /sys/class/power_supply/battery/screen_off_chg \
    /sys/class/oplus_chg/battery/night_chg_enable \
    /sys/class/oplus_chg/common/night_chg_enable \
    /sys/class/oplus_chg/battery/cool_down \
    /sys/class/oplus_chg/common/cool_down \
    /sys/class/power_supply/battery/input_suspend \
    /sys/class/power_supply/battery/restricted_charging \
    /sys/class/power_supply/battery/batt_slate_mode \
    /sys/class/power_supply/battery/store_mode \
    /sys/class/power_supply/battery/batt_misc_event \
    /sys/class/power_supply/battery/mmi_chrg_dis \
    /sys/class/qcom-battery/restrict_chg \
    /sys/devices/platform/charger/screen_off_throttle \
    /sys/devices/platform/soc/soc:google,charger/charge_disable \
    /sys/devices/platform/vivo_battery/screen_off_charge \
    /sys/devices/platform/vivo_battery/charge_sleep_mode \
    /sys/class/power_supply/battery/screen_state \
    /sys/class/hw_power/charger/charge_data/iin_thermal_aux \
    /sys/class/power_supply/battery/smart_charging_activation_enabled; do
    [ -w "$n" ] && printf '0\n' > "$n" 2>/dev/null
done
for n in \
    /sys/class/oplus_chg/battery/mmi_charging_enable \
    /sys/class/power_supply/battery/battery_charging_enabled \
    /sys/class/oplus_chg/battery/battery_charging_enabled \
    /sys/class/oplus_chg/battery/pd_allow \
    /sys/class/oplus_chg/common/pd_allow \
    /sys/class/oplus_chg/battery/vooc_allow \
    /sys/class/oplus_chg/battery/fast_chg_allow \
    /sys/kernel/oplus_chg/battery/voocphy_enable \
    /sys/class/oplus_chg/battery/wls_boost_en \
    /sys/class/oplus_chg/battery/wired_boost_enable \
    /sys/class/oplus_chg/common/wired_boost_enable; do
    [ -w "$n" ] && printf '1\n' > "$n" 2>/dev/null
done

setprop persist.vendor.oplus.charge.cooldown      0 2>/dev/null
setprop persist.oplus.chg.pps.allow               1 2>/dev/null
setprop persist.oplus.chg.ufcs.allow              1 2>/dev/null
setprop persist.oplus.chg.vooc.allow              1 2>/dev/null
setprop persist.oplus.chg.voocphy.allow           1 2>/dev/null
setprop persist.oplus.chg.svooc.allow             1 2>/dev/null
setprop persist.oplus.chg.flash_charge.allow      1 2>/dev/null
setprop vendor.battery.charge.disable             0 2>/dev/null
setprop persist.sys.powerhal.thermal.disabled     1 2>/dev/null
setprop persist.sys.oplus.wifi.sla.game_high_temperature 50 2>/dev/null
setprop persist.sys.environment.temp              25 2>/dev/null
setprop persist.oplus.chg.screenoff.decharge      0 2>/dev/null
setprop persist.oplus.chg.screen_off_decharge     0 2>/dev/null
setprop persist.oplus.chg.screen_off_slow_chg     0 2>/dev/null
setprop persist.oplus.chg.night_charging          0 2>/dev/null
setprop persist.oplus.chg.cool_down               0 2>/dev/null
setprop persist.miui.charge.screenoff             0 2>/dev/null
setprop persist.sys.charge.screenoff              0 2>/dev/null
setprop persist.sys.miui_charge_screen_off        0 2>/dev/null
setprop persist.sys.charge.sleep_mode             0 2>/dev/null
setprop persist.chg.screen_off_reduce             0 2>/dev/null
setprop persist.vendor.chg.screen_off_reduce      0 2>/dev/null
setprop vendor.battery.screenoff.decharge         0 2>/dev/null
setprop persist.vendor.battery.sleep_charging     0 2>/dev/null
setprop persist.vendor.dgb.sleep.chg.enabled      0 2>/dev/null
setprop persist.hw.charge.screenoff               0 2>/dev/null
setprop persist.vivo.charge.screenoff             0 2>/dev/null
setprop persist.vivo.charger.sleep_mode           0 2>/dev/null
setprop persist.mmi.charge.screenoff              0 2>/dev/null
setprop persist.samsung.charge.screenoff          0 2>/dev/null
setprop vendor.battery.restrict.charging          0 2>/dev/null
setprop persist.sys.charge.restrict               0 2>/dev/null

for n in \
    /sys/module/msm_thermal/parameters/enabled \
    /sys/module/msm_thermal/core_control/enabled \
    /sys/kernel/msm_thermal/enabled \
    /proc/oplus_temp/oplus_thermal_enable \
    /proc/oplus_temp/disable_thermal; do
    [ -w "$n" ] && printf '0\n' > "$n" 2>/dev/null
done

ui_print "  ✓ 充电速度最大化完成"

mtk_t=/vendor/etc/thermal
if [ -d "$mtk_t" ]; then
    mkdir -p "$module/system$mtk_t"
    for file in $(ls "$mtk_t"); do
        case $file in
            disable_*|fix_ttj_95.conf)
                cp "$mtk_t/$file" "$module/system$mtk_t/" ;;
            fix_ttj_85.conf)
                cp "$mtk_t/fix_ttj_95.conf" "$module/system$mtk_t/" ;;
            *)
                if [ -f "$mtk_t/disable_skin_control.conf" ]; then
                    cp "$mtk_t/disable_skin_control.conf" "$module/system$mtk_t/$file"
                else
                    cp "$mtk_t/fix_ttj_95.conf" "$module/system$mtk_t/$file"
                fi ;;
        esac
    done
    ui_print "  ✓ 天机热控配置覆盖完成"
fi

handle_partition() {
    if [ -L "/system/$1" ] && [ "$(readlink -f "/system/$1")" = "/$1" ]; then
        [ -e "$module/system/$1" ] && mv -f "$module/system/$1" "$module/$1"
        [ -e "../$1" ] && ln -sf "../$1" "$module/system/$1"
    fi
}
mkdir -p "$module/system"
handle_partition 'vendor'
handle_partition 'system_ext'
handle_partition 'product'

for C in extreme_gt murongruyan; do
    [ -d "/data/adb/modules/$C" ] && touch "/data/adb/modules/$C/remove" \
        && ui_print "  已标记清理冲突模块: $C"
done

for bp in /sys/class/power_supply/battery /sys/class/power_supply/Battery /sys/class/power_supply/mtk-battery; do
    [ -d "$bp" ] && ui_print "  ✓ 电池路径: $bp" && break
done

set_perm_recursive "$MODPATH" 0 0 0777 0777 u:object_r:system_file:s0
TOUCH_CONF="/data/adb/modules/AAaTempSpoof/AaTempSpoof/touch_opt.conf"
[ -f "$TOUCH_CONF" ] || cp "$MODPATH/AaTempSpoof/touch_opt.conf" "$TOUCH_CONF" 2>/dev/null

ui_print " "
ui_print "================================"
ui_print "  ✓ AaTempSpoof 安装完成"
ui_print "  ✓ 全机型去温控已应用"
ui_print "  ✓ 最大化充电速度已配置"
ui_print "  ✓ 重启后生效"
ui_print "  配置: AaTempSpoof/AaTempSpoof.txt"
ui_print "================================"
ui_print " "

_OLD_CONF="/data/adb/modules/AAaTempSpoof/AaTempSpoof"
_NEW_CONF="$MODPATH/AaTempSpoof"
if [ "$WORK_MODE" = "advanced" ] && [ -d "$_OLD_CONF" ]; then
    ui_print "  ┌─────────────────────────────────────┐"
    ui_print "  │  检测到旧版配置文件"
    ui_print "  ├─────────────────────────────────────┤"
    ui_print "  │  【音量+】迁移旧配置（不推荐）"
    ui_print "  │  【音量-】恢复默认配置继续（推荐）"
    ui_print "  └─────────────────────────────────────┘"
    ui_print " "
    wait_volume_key
    if [ "$VOLKEY_RESULT" = "up" ]; then
        _migrated=0
        for _src in "$_OLD_CONF"/*.txt "$_OLD_CONF"/*.conf; do
            [ -f "$_src" ] || continue
            _fname=$(basename "$_src")
            _dst="$_NEW_CONF/$_fname"
            cp -f "$_src" "$_dst" && _migrated=$(( _migrated + 1 ))
        done
        ui_print "  ➜ 配置迁移完成，共迁移 ${_migrated} 个文件"
    else
        ui_print "  ➜ 使用默认配置"
    fi
fi
ui_print " "

if [ "$WORK_MODE" = "advanced" ]; then
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  是否安装 KsuWebUI？"
ui_print "  │  (阿尔法/德尔塔用户务必安装，AP/KSU 无需)"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  【音量+】安装 KsuWebUI"
ui_print "  │  【音量-】跳过，继续安装"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "up" ]; then
    ui_print "  ➜ 正在跳转 KsuWebUI 下载页..."
    am start -a android.intent.action.VIEW -d "https://wwbjv.lanzn.com/iqtdz3kvro4f" 2>/dev/null
    ui_print "  ✓ 已跳转，请在浏览器中下载安装"
else
    ui_print "  ➜ 跳过，继续"
fi
fi

ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  是否安装 mountify 元模块？"
ui_print "  │  (为伪装提供 bind-mount 环境支持)"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  【音量+】安装 mountify 元模块"
ui_print "  │  【音量-】跳过，继续安装"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "up" ]; then
    ui_print "  ➜ 正在跳转 mountify 下载页..."
    am start -a android.intent.action.VIEW -d "https://wwbjv.lanzn.com/ima9I3ol64cb" 2>/dev/null
    ui_print "  ✓ 已跳转，请在浏览器中下载安装"
else
    ui_print "  ➜ 跳过，继续"
fi

ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  是否安装 AaTempSpoof 快捷开关 APK？  │"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  【音量+】安装"
ui_print "  │          部署 com.aatempspoof.tile"
ui_print "  │          可前往控制中心添加快捷开关控制总开关"
ui_print "  │  【音量-】跳过，继续安装模块"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "up" ]; then
    APK_PATH="$MODPATH/apk/AaTempSpoof.apk"
    if [ -f "$APK_PATH" ]; then
        pm install "$APK_PATH" 2>/dev/null
        ui_print "  ✓ 已部署 com.aatempspoof.tile"
        ui_print "  可前往控制中心添加快捷开关控制总开关"
    else
        ui_print "  ✗ 未找到 AaTempSpoof.apk，跳过安装"
    fi
else
    ui_print "  ➜ 跳过，继续安装模块"
fi
ui_print " "
ui_print "================================"
ui_print "     感谢使用 AaTempSpoof！"
ui_print "================================"
ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  【1/4】按任意音量键"
ui_print "  │       → 关注作者酷安主页 😋"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
ui_print "  ➜ 跳转酷安主页..."
am start -a android.intent.action.VIEW -d "https://www.coolapk.com/u/33802586" 2>/dev/null
ui_print "  ✓ 已跳转，请在浏览器查看"
ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  是否加入 QQ玩机模块交流群？"
ui_print "  ├─────────────────────────────────────┤"
ui_print "  │  【音量+】是，继续跳转加入"
ui_print "  │  【音量-】否，跳过不加入"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
if [ "$VOLKEY_RESULT" = "up" ]; then
ui_print "  ➜ 继续跳转加入交流群"
ui_print " "
ui_print " (飒🖊️一直点随机封，建议都加)"
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  【2/4】按任意音量键"
ui_print "  │       → 加入 QQ 交流1群 😋"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
ui_print "  ➜ 跳转 QQ 交流1群..."
am start -a android.intent.action.VIEW \
    -d "https://qun.qq.com/universal-share/share?ac=1&authKey=qUhP%2FTjVn%2Bn8Kj2o1HuuqixdUJuIL%2FUqipTmHg3wH%2FYRpIu%2FvJGpp9t4oxt5H0M9&busi_data=eyJncm91cENvZGUiOiIxMDk1ODk3MjY2IiwidG9rZW4iOiIrT2IzLzRDbE9VcHNYcktRU2lydVFqekptUEFNOWFWWGVtMS81c1I5K0wyaGJOTCtaNm1JUDhaUFR5Y2JDQ0Y5IiwidWluIjoiMzg0MjU4NTQwMCJ9&data=r_723xAFbvzT_FsP05EzXFhKwDqyGBcD8c3nYW9cw4Ono-GM2H22HKQwi219i8pnyzn2EihXbJodDCyrfHt51w&svctype=4&tempid=h5_group_info" 2>/dev/null
ui_print "  ✓ 已跳转，请在浏览器查看"
ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  【3/4】按任意音量键"
ui_print "  │       → 加入 QQ 交流2群 😋"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
ui_print "  ➜ 跳转 QQ 交流2群..."
am start -a android.intent.action.VIEW \
    -d "https://qun.qq.com/universal-share/share?ac=1&authKey=HNa02hbfYJ0rs87Y9fwbETiN2ZndtNoDurewMafx7e5VlplP4gRop3GZuxWA0QIC&busi_data=eyJncm91cENvZGUiOiIyMjg5NTA1NDIiLCJ0b2tlbiI6IjRCcmNrOVpiWWdYOW02WkdIRkRjZWdiY3RnYzQ5MHFxczBoNEdTWitCbnAwaFZZNDR3VFovWnFrcVA1SFNiMEwiLCJ1aW4iOiIzNjQyMTMyNjYwIn0%3D&data=KBz2uxPUJglfFtVFQSeuIZ0s4KltMyJ3gBw-i83NUrDeqdLDVu18dGzrDReKNk95N5iv_V4OmLvZ0dRmbQkS9A&svctype=4&tempid=h5_group_info" 2>/dev/null
ui_print "  ✓ 已跳转，请在浏览器查看"
ui_print " "
ui_print "  ┌─────────────────────────────────────┐"
ui_print "  │  【4/4】按任意音量键"
ui_print "  │       → 加入 QQ 交流3群 😋"
ui_print "  └─────────────────────────────────────┘"
ui_print " "
wait_volume_key
ui_print "  ➜ 跳转 QQ 交流3群..."
am start -a android.intent.action.VIEW \
    -d "https://qun.qq.com/universal-share/share?ac=1&authKey=xuJoEpN7JyDyKz2WBJnXqpUagvivlYRy%2FurGD%2FIGb%2BxfNTMWcUH%2FSsfymQRlnedR&busi_data=eyJncm91cENvZGUiOiIxMDk0ODUyMjMwIiwidG9rZW4iOiJjL3d1TGZEbXd4TTZXOFlaeEc3UVY0a3BLUzlEbGdYcUpWWlpIcmh0VU5Uemlick1qQ3NyYzF6VjBFR2FlMkNLIiwidWluIjoiMzY0MjEzMjY2MCJ9&data=jJZxH4aAZIazR5UCpZkQMkFjxaQxfuc_L-2yyLcjLeedHu6jSuDy7qEHSZkHsoh4jlmiv8eB-P9G06ODusy1WQ&svctype=4&tempid=h5_group_info" 2>/dev/null
ui_print "  ✓ 已跳转，请在浏览器查看"
else
ui_print "  ➜ 跳过加入交流群，继续安装"
fi
ui_print " "
ui_print "  重启后伪装生效，祝使用愉快！"
rm -rf /data/adb/modules_update/AAaTempSpoof/KsuWebUI.apk 2>/dev/null
