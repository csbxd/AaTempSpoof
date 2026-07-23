package com.aatempspoof.tile;

import android.animation.ValueAnimator;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.service.quicksettings.TileService;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;

public class MainActivity extends AppCompatActivity {

    private static final int COLOR_TOGGLE_ON   = 0xFF06B6D4;
    private static final int COLOR_TOGGLE_OFF  = 0xFF1E293B;
    private static final int COLOR_TEXT_ON     = 0xFF06B6D4;
    private static final int COLOR_ROOT_OK     = 0xFF22C55E;
    private static final int COLOR_ROOT_FAIL   = 0xFFEF4444;

    private static final String UPDATE_URL =
            "https://raw.githubusercontent.com/csbxd/AaTempSpoof/main/update.json";
    private static final String DOWNLOAD_URL =
            "https://wwbjv.lanzn.com/b0188e5u6j";
    private static final String DOWNLOAD_PASSWORD = "1lek";

    private FrameLayout   flToggle;
    private ImageView     ivThermometer;
    private ImageView     ivRootIcon;
    private TextView      tvRootStatus;
    private TextView      tvSwitchStatus;
    private TextView      tvSubHint;
    private LinearLayout  llRetryRoot;
    private TextView      btnCheckUpdate;

    private boolean hasRoot      = false;
    private boolean currentState = false;
    private boolean isBusy       = false;

    private final GradientDrawable toggleCircle = new GradientDrawable();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    // ═══════════════════════════════════════════════════════════════════════════
    // 生命周期
    // ═══════════════════════════════════════════════════════════════════════════

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        bindViews();
        initToggleDrawable();
        checkRoot();
        checkForUpdate(false);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (hasRoot) loadState();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 绑定视图
    // ═══════════════════════════════════════════════════════════════════════════

    private void bindViews() {
        flToggle       = findViewById(R.id.fl_toggle);
        ivThermometer  = findViewById(R.id.iv_thermometer);
        ivRootIcon     = findViewById(R.id.iv_root_icon);
        tvRootStatus   = findViewById(R.id.tv_root_status);
        tvSwitchStatus = findViewById(R.id.tv_switch_status);
        tvSubHint      = findViewById(R.id.tv_sub_hint);
        llRetryRoot    = findViewById(R.id.ll_retry_root);

        flToggle.setOnClickListener(v -> onToggleClicked());

        View btnRetry = findViewById(R.id.btn_retry_root);
        if (btnRetry != null) {
            btnRetry.setOnClickListener(v -> {
                llRetryRoot.setVisibility(View.GONE);
                RootUtils.invalidateRootCache();
                checkRoot();
            });
        }

        // 检测更新按钮
        btnCheckUpdate = findViewById(R.id.btn_check_update);
        if (btnCheckUpdate != null) {
            btnCheckUpdate.setOnClickListener(v -> checkForUpdate(true));
        }
    }

    private void initToggleDrawable() {
        toggleCircle.setShape(GradientDrawable.OVAL);
        toggleCircle.setColor(COLOR_TOGGLE_OFF);
        flToggle.setBackground(toggleCircle);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 更新检查
    // ═══════════════════════════════════════════════════════════════════════════

    /** @param showLatest true=手动触发需要反馈；false=自动静默检测 */
    private void checkForUpdate(boolean showLatest) {
        if (showLatest) {
            Toast.makeText(this, R.string.update_checking, Toast.LENGTH_SHORT).show();
        }

        new Thread(() -> {
            try {
                PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
                final String localVersion = pi.versionName;

                String remoteVersion = fetchRemoteVersion();
                if (remoteVersion == null || remoteVersion.isEmpty()) {
                    if (showLatest) {
                        mainHandler.post(() -> Toast.makeText(this,
                                R.string.update_already_latest, Toast.LENGTH_SHORT).show());
                    }
                    return;
                }

                if (!localVersion.equals(remoteVersion)) {
                    final String finalRemote = remoteVersion;
                    mainHandler.post(() -> showUpdateDialog(localVersion, finalRemote));
                } else if (showLatest) {
                    mainHandler.post(() -> Toast.makeText(this,
                            R.string.update_already_latest, Toast.LENGTH_SHORT).show());
                }
            } catch (Exception ignored) {
                if (showLatest) {
                    mainHandler.post(() -> Toast.makeText(this,
                            R.string.update_already_latest, Toast.LENGTH_SHORT).show());
                }
            }
        }).start();
    }

    private String fetchRemoteVersion() {
        HttpURLConnection conn = null;
        try {
            URL url = new URL(UPDATE_URL);
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(5000);
            conn.setReadTimeout(5000);
            conn.setRequestMethod("GET");

            int code = conn.getResponseCode();
            if (code != HttpURLConnection.HTTP_OK) return null;

            BufferedReader reader = new BufferedReader(
                    new InputStreamReader(conn.getInputStream(), "UTF-8"));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line);
            }
            reader.close();

            JSONObject metadata = new JSONObject(sb.toString());
            String version = metadata.optString("version", "").trim();
            return version.startsWith("v") ? version.substring(1) : version;
        } catch (Exception e) {
            return null;
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    private void showUpdateDialog(String localVersion, String remoteVersion) {
        if (isFinishing() || isDestroyed()) return;

        String message = getString(R.string.update_message, remoteVersion, localVersion);

        new AlertDialog.Builder(this)
                .setTitle(R.string.update_title)
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton(R.string.update_btn_go, (dialog, which) -> {
                    showPasswordDialog();
                })
                .setNegativeButton(R.string.update_btn_later, (dialog, which) -> dialog.dismiss())
                .show();
    }

    private void showPasswordDialog() {
        if (isFinishing() || isDestroyed()) return;

        String message = getString(R.string.update_password_message, DOWNLOAD_PASSWORD);

        new AlertDialog.Builder(this)
                .setTitle(R.string.update_password_title)
                .setMessage(message)
                .setCancelable(false)
                .setPositiveButton(R.string.update_btn_copy_password, (dialog, which) -> {
                    // 复制密码到剪贴板
                    ClipboardManager cm = (ClipboardManager)
                            getSystemService(Context.CLIPBOARD_SERVICE);
                    ClipData clip = ClipData.newPlainText("password", DOWNLOAD_PASSWORD);
                    cm.setPrimaryClip(clip);
                    Toast.makeText(this,
                            R.string.update_password_copied, Toast.LENGTH_SHORT).show();
                    // 复制后直接跳转下载
                    Intent intent = new Intent(Intent.ACTION_VIEW,
                            Uri.parse(DOWNLOAD_URL));
                    startActivity(intent);
                })
                .setNegativeButton(R.string.update_btn_download, (dialog, which) -> {
                    Intent intent = new Intent(Intent.ACTION_VIEW,
                            Uri.parse(DOWNLOAD_URL));
                    startActivity(intent);
                })
                .show();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Root 检测
    // ═══════════════════════════════════════════════════════════════════════════

    private void checkRoot() {
        setBusy(true);
        showRootChecking();
        new Thread(() -> {
            boolean root = RootUtils.hasRoot();
            mainHandler.post(() -> {
                hasRoot = root;
                setBusy(false);
                if (hasRoot) {
                    showRootGranted();
                    flToggle.setEnabled(true);
                    loadState();
                } else {
                    showRootDenied();
                    flToggle.setEnabled(false);
                    applyToggleUI(false, false);
                }
            });
        }).start();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // 开关状态
    // ═══════════════════════════════════════════════════════════════════════════

    private void loadState() {
        setBusy(true);
        new Thread(() -> {
            boolean on = RootUtils.readSwitchState();
            mainHandler.post(() -> {
                currentState = on;
                applyToggleUI(on, true);
                setBusy(false);
            });
        }).start();
    }

    private void onToggleClicked() {
        if (!hasRoot) {
            Toast.makeText(this, R.string.no_root_toast, Toast.LENGTH_SHORT).show();
            return;
        }
        if (isBusy) return;

        boolean newState = !currentState;
        setBusy(true);

        flToggle.animate().scaleX(0.88f).scaleY(0.88f).setDuration(100)
                .withEndAction(() -> flToggle.animate().scaleX(1f).scaleY(1f)
                        .setDuration(150).setInterpolator(new DecelerateInterpolator(2f)).start())
                .start();

        new Thread(() -> {
            boolean success = RootUtils.writeSwitchState(newState);
            mainHandler.post(() -> {
                setBusy(false);
                if (success) {
                    currentState = newState;
                    applyToggleUI(newState, true);
                    notifyTile();
                } else {
                    Toast.makeText(this, R.string.write_error, Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // UI 更新
    // ═══════════════════════════════════════════════════════════════════════════

    private int getTextOffColor() {
        return getResources().getColor(R.color.text_secondary, getTheme());
    }

    private void applyToggleUI(boolean on, boolean animate) {
        ColorStateList csl = toggleCircle.getColor();
        int fromColor = (csl != null) ? csl.getDefaultColor()
                                      : (on ? COLOR_TOGGLE_OFF : COLOR_TOGGLE_ON);
        int toColor = on ? COLOR_TOGGLE_ON : COLOR_TOGGLE_OFF;

        if (animate && fromColor != toColor) {
            ValueAnimator colorAnim = ValueAnimator.ofArgb(fromColor, toColor);
            colorAnim.setDuration(300);
            colorAnim.setInterpolator(new DecelerateInterpolator());
            colorAnim.addUpdateListener(va -> {
                toggleCircle.setColor((int) va.getAnimatedValue());
                flToggle.setBackground(toggleCircle);
            });
            colorAnim.start();
        } else {
            toggleCircle.setColor(toColor);
            flToggle.setBackground(toggleCircle);
        }

        tvSwitchStatus.setText(on ? R.string.switch_on : R.string.switch_off);
        tvSwitchStatus.setTextColor(on ? COLOR_TEXT_ON : getTextOffColor());
        tvSubHint.setText(on ? R.string.sub_hint_on : R.string.sub_hint_off);
    }

    private void showRootChecking() {
        tvRootStatus.setText(R.string.root_checking);
        tvRootStatus.setTextColor(getTextOffColor());
        ivRootIcon.setImageResource(R.drawable.ic_root_checking);
        if (llRetryRoot != null) llRetryRoot.setVisibility(View.GONE);
    }

    private void showRootGranted() {
        tvRootStatus.setText(R.string.root_granted);
        tvRootStatus.setTextColor(COLOR_ROOT_OK);
        ivRootIcon.setImageResource(R.drawable.ic_root_ok);
        if (llRetryRoot != null) llRetryRoot.setVisibility(View.GONE);
    }

    private void showRootDenied() {
        tvRootStatus.setText(R.string.root_denied);
        tvRootStatus.setTextColor(COLOR_ROOT_FAIL);
        ivRootIcon.setImageResource(R.drawable.ic_root_fail);
        if (llRetryRoot != null) llRetryRoot.setVisibility(View.VISIBLE);
        applyToggleUI(false, false);
        tvSwitchStatus.setText(R.string.switch_no_root);
        tvSwitchStatus.setTextColor(COLOR_ROOT_FAIL);
    }

    private void setBusy(boolean busy) {
        isBusy = busy;
        flToggle.setAlpha(busy ? 0.55f : 1f);
    }

    private void notifyTile() {
        TileService.requestListeningState(this,
                new ComponentName(this, TempSpoofTileService.class));
    }
}
