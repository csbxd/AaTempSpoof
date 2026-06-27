#!/system/bin/sh
MODDIR=${0%/*}

replace_files() {
    local folder="$1"
    find "$MODDIR/$folder" -type f 2>/dev/null | while read -r src; do
        local dst="${src#$MODDIR}"
        [ -f "$dst" ] && mount --bind "$src" "$dst"
    done
}

mount_folders='my_product my_heytap my_stock odm'
if [ "$KSU" = "true" ] || [ "$(which ksud)" != "" ] || [ "$(which apd)" != "" ]; then
    mount_folders='my_product my_heytap my_stock'
fi

for folder in $mount_folders; do
    [ -d "$MODDIR/$folder" ] && replace_files "$folder"
done

stop oppo_theias
stop thermal_mnt_hal_service
stop orms-hal-1-0
stop fuelgauged
stop smartcharging