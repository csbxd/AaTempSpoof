package com.aatempspoof.tile;

import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;
import android.widget.Toast;

/**
 * 快捷控制中心 Tile 服务。
 * 点击 Tile → 切换 总开关.txt 内容（0/1）。
 * Tile 状态实时反映文件内容。
 */
public class TempSpoofTileService extends TileService {

    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    // ─── 生命周期 ─────────────────────────────────────────────────────────────

    @Override
    public void onStartListening() {
        super.onStartListening();
        // QS 面板展开时刷新 Tile 状态
        refreshTileAsync();
    }

    @Override
    public void onStopListening() {
        super.onStopListening();
    }

    // ─── 点击处理 ──────────────────────────────────────────────────────────────

    @Override
    public void onClick() {
        super.onClick();

        // 立即显示"处理中"，提供即时反馈
        setTileProcessing();

        new Thread(() -> {
            // 1. 检查 Root
            if (!RootUtils.hasRoot()) {
                mainHandler.post(() -> {
                    Toast.makeText(this,
                            getString(R.string.no_root_toast),
                            Toast.LENGTH_SHORT).show();
                    // 强制刷新（还原 processing 状态）
                    refreshTileAsync();
                });
                return;
            }

            // 2. 读取当前状态并取反
            boolean currentOn = RootUtils.readSwitchState();
            boolean newOn = !currentOn;

            // 3. 写入文件
            boolean success = RootUtils.writeSwitchState(newOn);

            mainHandler.post(() -> {
                if (success) {
                    applyTileState(newOn);
                } else {
                    Toast.makeText(this,
                            getString(R.string.write_error),
                            Toast.LENGTH_SHORT).show();
                    // 写入失败，回滚显示
                    applyTileState(currentOn);
                }
            });
        }).start();
    }

    // ─── Tile UI 更新 ──────────────────────────────────────────────────────────

    /** 后台线程检测状态，主线程更新 Tile。 */
    private void refreshTileAsync() {
        new Thread(() -> {
            if (!RootUtils.hasRoot()) {
                mainHandler.post(this::applyTileUnavailable);
                return;
            }
            boolean on = RootUtils.readSwitchState();
            mainHandler.post(() -> applyTileState(on));
        }).start();
    }

    /** Tile 显示为"处理中"，给用户即时反馈。 */
    private void setTileProcessing() {
        Tile tile = getQsTile();
        if (tile == null) return;
        tile.setLabel(getString(R.string.tile_processing));
        tile.setState(Tile.STATE_INACTIVE);
        tile.updateTile();
    }

    /** 根据开关状态更新 Tile UI。 */
    private void applyTileState(boolean on) {
        Tile tile = getQsTile();
        if (tile == null) return;

        tile.setIcon(Icon.createWithResource(this, R.drawable.ic_tile));
        tile.setState(on ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setLabel(getString(R.string.tile_label));

        // Subtitle 需要 Android 10+（API 29）
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            tile.setSubtitle(getString(
                    on ? R.string.tile_subtitle_on : R.string.tile_subtitle_off));
        }
        tile.updateTile();
    }

    /** 无 Root 权限时将 Tile 置为不可用状态。 */
    private void applyTileUnavailable() {
        Tile tile = getQsTile();
        if (tile == null) return;

        tile.setIcon(Icon.createWithResource(this, R.drawable.ic_tile));
        tile.setState(Tile.STATE_UNAVAILABLE);
        tile.setLabel(getString(R.string.tile_label));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            tile.setSubtitle(getString(R.string.no_root_short));
        }
        tile.updateTile();
    }
}
