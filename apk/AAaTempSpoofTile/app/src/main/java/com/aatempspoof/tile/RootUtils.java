package com.aatempspoof.tile;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.concurrent.TimeUnit;

public class RootUtils {

    /** 目标文件路径 */
    static final String SWITCH_FILE =
            "/data/adb/modules/AAaTempSpoof/AaTempSpoof/\u603b\u5f00\u5173.txt";
    // \u603b\u5f00\u5173 = 总开关

    /** Root可用性缓存，30秒有效 */
    private static Boolean sRootCache = null;
    private static long sRootCacheTime = 0L;
    private static final long ROOT_CACHE_TTL = 30_000L;

    // ─────────────────────────────────────────────────────────────────────────
    // 核心执行方法
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * 以 root 身份运行 shell 命令，返回 stdout 字符串，失败返回 null。
     * 最多等待 5 秒，超时强制结束。
     */
    static String runAsRoot(String command) {
        Process proc = null;
        try {
            proc = Runtime.getRuntime().exec("su");
            OutputStream stdin = proc.getOutputStream();

            stdin.write((command + "\n").getBytes("UTF-8"));
            stdin.write("exit\n".getBytes("UTF-8"));
            stdin.flush();
            stdin.close();

            StringBuilder sb = new StringBuilder();
            try (BufferedReader br = new BufferedReader(
                    new InputStreamReader(proc.getInputStream(), "UTF-8"))) {
                String line;
                while ((line = br.readLine()) != null) {
                    if (sb.length() > 0) sb.append('\n');
                    sb.append(line);
                }
            }

            boolean done = proc.waitFor(5, TimeUnit.SECONDS);
            if (!done) {
                proc.destroyForcibly();
                return null;
            }
            return sb.toString();

        } catch (Exception e) {
            return null;
        } finally {
            if (proc != null) proc.destroy();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Root 检测
    // ─────────────────────────────────────────────────────────────────────────

    /** 检测 Root 权限（带 30s 缓存，避免频繁弹窗）。 */
    public static boolean hasRoot() {
        long now = System.currentTimeMillis();
        if (sRootCache != null && (now - sRootCacheTime) < ROOT_CACHE_TTL) {
            return sRootCache;
        }
        String result = runAsRoot("id");
        sRootCache = (result != null && result.contains("uid=0"));
        sRootCacheTime = now;
        return sRootCache;
    }

    /** 强制刷新 Root 缓存（用户重试授权时调用）。 */
    public static void invalidateRootCache() {
        sRootCache = null;
        sRootCacheTime = 0L;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 开关文件操作
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * 读取当前开关状态。
     * 文件内容为 "1" → true（开），其他 / 不存在 → false（关）。
     */
    public static boolean readSwitchState() {
        // 文件不存在时 fallback echo 0
        String r = runAsRoot(
                "cat '" + SWITCH_FILE + "' 2>/dev/null || echo '0'");
        return r != null && "1".equals(r.trim());
    }

    /**
     * 写入开关状态并用 echo _OK_ 验证成功。
     * @param on true → 写 "1"，false → 写 "0"
     * @return true 表示写入成功
     */
    public static boolean writeSwitchState(boolean on) {
        String val = on ? "1" : "0";
        // printf 避免换行，单引号保护中文路径
        String cmd = "printf '%s' '" + val + "' > '" + SWITCH_FILE
                + "' && echo _OK_";
        String r = runAsRoot(cmd);
        return r != null && r.contains("_OK_");
    }
}
