#!/system/bin/sh

clear
export LANG=C.UTF-8

echo "============================================="
echo "温度传感器一键查询"
echo "============================================="

printf "\n本机所有有效温度传感器\n"
echo "传感器类型		温度"
echo "---------------------------------------------"

front_shell=""
middle_shell=""
back_shell=""
battery_temp=""

for zone in /sys/class/thermal/thermal_zone*; do
    [ -f "$zone/type" ] && [ -f "$zone/temp" ] && [ -s "$zone/type" ] && [ -s "$zone/temp" ] || continue
    sensor_type=$(cat "$zone/type" 2>/dev/null)
    case "$sensor_type" in
        *vbat*|*volt*|*current*|*power*|*soc_volt*)
            continue
            ;;
    esac
    raw=$(cat "$zone/temp" 2>/dev/null)
    case "${raw:-x}" in ''|*[!0-9]*) continue ;; esac
    [ "$raw" -ge 0 ] 2>/dev/null || continue
    temp_c=$(awk "BEGIN{printf \"%.1f\", $raw/1000}")
    [ "$temp_c" = "0.0" ] && continue
    printf "%-16s\t%4s ℃\n" "$sensor_type" "$temp_c"
    case "$sensor_type" in
        shell_front*) front_shell="$temp_c" ;;
        shell_frame*) middle_shell="$temp_c" ;;
        shell_back*)  back_shell="$temp_c"  ;;
        *battery*)    battery_temp="$temp_c" ;;
    esac
done

printf "\n=============================================\n"
echo "机身核心重点温度"
echo "---------------------------------------------"
printf "前壳温度:\t\t%s ℃\n" "${front_shell:---}"
printf "中壳温度:\t\t%s ℃\n" "${middle_shell:---}"
printf "后壳温度:\t\t%s ℃\n" "${back_shell:---}"
printf "电池温度:\t\t%s ℃\n" "${battery_temp:---}"
echo "============================================="
echo "查询完成"
echo "按任意音量键退出"
echo "============================================="

while true; do
    ev=$(getevent -lqc 1 2>/dev/null)
    case "$ev" in
        *KEY_VOLUMEUP*DOWN*|*KEY_VOLUMEDOWN*DOWN*) break ;;
    esac
done