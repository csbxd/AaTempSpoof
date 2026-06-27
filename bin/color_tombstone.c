#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <time.h>
#include <stdarg.h>
#include <libgen.h>
#include <limits.h>

#define LOG_TAG    "ColorTombstone"
#define LOG_FILE   "/data/adb/modules/AAaTempSpoof/AaTempSpoof/tempspoof.log"

#define SWITCH_FILE \
    "/data/adb/modules/AAaTempSpoof/AaTempSpoof/mubei.txt"

#define CGROUP_FROZEN   "/sys/fs/cgroup/frozen"
#define CGROUP_UNFROZEN "/sys/fs/cgroup/unfrozen"

#define PROP_VAL_MAX        128
#define INOTIFY_EVENT_SIZE  (sizeof(struct inotify_event) + 256)
#define INOTIFY_BUF_SIZE    (INOTIFY_EVENT_SIZE * 8)

static FILE *g_log_fp = NULL;

static void log_open(void)  { g_log_fp = fopen(LOG_FILE, "a"); }
static void log_close(void) { if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; } }

static void log_print(const char *level, const char *fmt, ...)
{
    char tb[32];
    time_t now = time(NULL);
    strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list ap;
    fprintf(stderr, "[%s][%s][%s] ", tb, LOG_TAG, level);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");

    if (g_log_fp) {
        fprintf(g_log_fp, "[%s][%s][%s] ", tb, LOG_TAG, level);
        va_start(ap, fmt); vfprintf(g_log_fp, fmt, ap); va_end(ap);
        fprintf(g_log_fp, "\n");
        fflush(g_log_fp);
    }
}

#define LOG_I(fmt, ...)  log_print("INFO ", fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...)  log_print("WARN ", fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...)  log_print("ERROR", fmt, ##__VA_ARGS__)
#define LOG_OK(fmt, ...) log_print("OK   ", fmt, ##__VA_ARGS__)

typedef struct {
    const char *key;
    const char *target_val;
    char        orig_val[PROP_VAL_MAX];
    int         orig_saved;
} PropEntry;

static PropEntry g_props[] = {
    { "vendor.perf.framepacing.enable", "false",     {0}, 0 },
    { "ro.boot.veritymode",             "enforcing", {0}, 0 },
    { "ro.oplus.radio.hide_nr_switch",  "0",         {0}, 0 },
    { "ro.oplus.audio.thermal_control", "0",         {0}, 0 },
    { "oplus.dex.tempcontrol",          "false",     {0}, 0 },
};
#define PROP_COUNT (sizeof(g_props) / sizeof(g_props[0]))

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fputs(content, fp);
    fclose(fp);
    return 0;
}

static int mkdir_p(const char *path)
{
    int ret = mkdir(path, 0755);
    return (ret < 0 && errno != EEXIST) ? -1 : 0;
}

static int shell_chown(const char *owner, const char *path)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "chown %s \"%s\" 2>/dev/null", owner, path);
    return system(cmd);
}

static int do_getprop(const char *key, char *val, size_t val_sz)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "getprop \"%s\" 2>/dev/null", key);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int ok = (fgets(val, (int)val_sz, fp) != NULL);
    pclose(fp);
    if (!ok) { val[0] = '\0'; return -1; }
    size_t len = strlen(val);
    if (len > 0 && val[len-1] == '\n') val[--len] = '\0';
    return (len > 0) ? 0 : -1;
}

static int do_resetprop(const char *key, const char *value)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "{ /data/adb/magisk/resetprop \"%s\" \"%s\" || "
             "resetprop \"%s\" \"%s\"; } 2>/dev/null",
             key, value, key, value);
    return system(cmd);
}

static void wait_boot_completed(void)
{
    LOG_I("等待系统启动完成 (sys.boot_completed)...");
    char val[16];
    for (;;) {
        if (do_getprop("sys.boot_completed", val, sizeof(val)) == 0
            && strcmp(val, "1") == 0) {
            LOG_OK("sys.boot_completed=1，系统已启动完成");
            return;
        }
        sleep(3);
    }
}

static void snapshot_original_props(void)
{
    LOG_I("快照原始属性值（共 %zu 条）...", PROP_COUNT);
    for (size_t i = 0; i < PROP_COUNT; i++) {
        PropEntry *e = &g_props[i];
        char tmp[PROP_VAL_MAX] = {0};
        if (do_getprop(e->key, tmp, sizeof(tmp)) == 0) {
            snprintf(e->orig_val, PROP_VAL_MAX, "%s", tmp);
            e->orig_saved = 1;
            LOG_I("  快照 %s = \"%s\"", e->key, e->orig_val);
        } else {
            e->orig_val[0] = '\0';
            e->orig_saved = 1;
            LOG_I("  快照 %s = <不存在>", e->key);
        }
    }
    LOG_OK("原始属性快照完成");
}

static int apply_props(int enable)
{
    const char *action = enable ? "写入目标值" : "还原原始值";
    LOG_I("属性操作: %s（共 %zu 条）", action, PROP_COUNT);
    int fail = 0;

    for (size_t i = 0; i < PROP_COUNT; i++) {
        PropEntry *e = &g_props[i];
        const char *write_val;
        char restore_cmd[512];

        if (enable) {
            write_val = e->target_val;
            if (do_resetprop(e->key, write_val) == 0)
                LOG_OK("  SET %s = \"%s\"", e->key, write_val);
            else {
                LOG_E("  SET 失败: %s = \"%s\"", e->key, write_val);
                fail++;
            }
        } else {
            if (!e->orig_saved) {
                LOG_W("  RESTORE 跳过 %s（未快照）", e->key);
                continue;
            }
            if (e->orig_val[0] == '\0') {
                snprintf(restore_cmd, sizeof(restore_cmd),
                         "{ /data/adb/magisk/resetprop --delete \"%s\" || "
                         "resetprop --delete \"%s\"; } 2>/dev/null",
                         e->key, e->key);
                if (system(restore_cmd) == 0)
                    LOG_OK("  DEL  %s（原本不存在，已删除）", e->key);
                else
                    LOG_W("  DEL  %s 失败（可能是系统固定属性，忽略）", e->key);
            } else {
                write_val = e->orig_val;
                if (do_resetprop(e->key, write_val) == 0)
                    LOG_OK("  RESTORE %s = \"%s\"", e->key, write_val);
                else {
                    LOG_E("  RESTORE 失败: %s = \"%s\"", e->key, write_val);
                    fail++;
                }
            }
        }
    }

    if (fail == 0) {
        LOG_OK("属性%s完成", action);
        return 0;
    }
    LOG_W("属性%s完成，%d 条失败（只读属性无法修改属于正常）", action, fail);
    return -1;
}

static int init_cgroup_dirs(void)
{
    LOG_I("初始化 cgroup v2 墓碑目录...");
    if (!path_exists("/sys/fs/cgroup")) {
        LOG_E("cgroup 根目录不存在，设备不支持 cgroup v2");
        return -1;
    }

    const char *dirs[] = { CGROUP_FROZEN, CGROUP_UNFROZEN };
    for (int i = 0; i < 2; i++) {
        if (mkdir_p(dirs[i]) != 0) {
            LOG_E("创建目录失败: %s (%s)", dirs[i], strerror(errno));
            return -1;
        }
        LOG_OK("目录就绪: %s", dirs[i]);
    }

    const char *nodes[] = {
        CGROUP_FROZEN   "/cgroup.procs",
        CGROUP_FROZEN   "/cgroup.freeze",
        CGROUP_UNFROZEN "/cgroup.procs",
        CGROUP_UNFROZEN "/cgroup.freeze",
    };
    for (int i = 0; i < 4; i++) {
        if (!path_exists(nodes[i])) {
            LOG_W("节点不存在，跳过 chown: %s", nodes[i]);
            continue;
        }
        if (shell_chown("system:system", nodes[i]) == 0)
            LOG_OK("chown 成功: %s", nodes[i]);
        else
            LOG_W("chown 失败（非致命）: %s", nodes[i]);
    }
    LOG_OK("cgroup 目录初始化完成");
    return 0;
}

static int g_current_state = -1;

static int apply_state(int enable)
{
    if (g_current_state == enable) {
        LOG_I("状态未变（%s），跳过", enable ? "开" : "关");
        return 0;
    }

    const char *desc = enable ? "开启" : "关闭";
    LOG_I("══ 墓碑切换 → %s ══", desc);

    apply_props(enable);

    const char *freeze_val = enable ? "1" : "0";
    const char *freeze_nodes[] = {
        CGROUP_FROZEN   "/cgroup.freeze",
        CGROUP_UNFROZEN "/cgroup.freeze",
    };
    int fail = 0;
    for (int i = 0; i < 2; i++) {
        if (!path_exists(freeze_nodes[i])) {
            LOG_W("节点不存在，跳过: %s", freeze_nodes[i]);
            continue;
        }
        if (write_file(freeze_nodes[i], freeze_val) == 0)
            LOG_OK("cgroup.freeze=%s → %s", freeze_val, freeze_nodes[i]);
        else {
            LOG_E("cgroup.freeze=%s 写入失败: %s (%s)",
                  freeze_val, freeze_nodes[i], strerror(errno));
            fail++;
        }
    }

    g_current_state = enable;
    if (fail == 0)
        LOG_OK("墓碑已%s ✓", desc);
    else
        LOG_W("墓碑%s完成，但 %d 个 cgroup 节点写入失败", desc, fail);
    return fail ? -1 : 0;
}

static int read_switch(void)
{
    FILE *fp = fopen(SWITCH_FILE, "r");
    if (!fp) {
        LOG_W("开关文件不存在，默认开启");
        return 1;
    }
    char buf[8] = {0};
    char *_r = fgets(buf, sizeof(buf), fp); (void)_r;
    fclose(fp);

    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' '))
        p[--len] = '\0';

    if (strcmp(p, "0") == 0) return 0;
    if (strcmp(p, "1") == 0) return 1;

    LOG_W("开关文件内容无效 (\"%s\")，默认开启", p);
    return 1;
}

static void watch_loop(void)
{
    char path_for_dir[512], path_for_name[512];
    strncpy(path_for_dir,  SWITCH_FILE, sizeof(path_for_dir)  - 1);
    strncpy(path_for_name, SWITCH_FILE, sizeof(path_for_name) - 1);
    const char *watch_dir  = dirname(path_for_dir);
    const char *watch_file = basename(path_for_name);

    LOG_I("启动 inotify 监听: %s / %s", watch_dir, watch_file);

    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd < 0) {
        LOG_E("inotify_init1 失败 (%s) → 降级轮询", strerror(errno));
        goto poll_fallback;
    }
    int wd = inotify_add_watch(ifd, watch_dir,
                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (wd < 0) {
        LOG_E("inotify_add_watch 失败 (%s) → 降级轮询", strerror(errno));
        close(ifd);
        goto poll_fallback;
    }
    LOG_OK("inotify 就绪，等待 %s 变化...", watch_file);

    char buf[INOTIFY_BUF_SIZE];
    for (;;) {
        ssize_t len = read(ifd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) continue;
            LOG_E("inotify read 失败 (%s)", strerror(errno));
            break;
        }
        int triggered = 0;
        char *ptr = buf;
        while (ptr < buf + len) {
            struct inotify_event *ev = (struct inotify_event *)ptr;
            if (ev->len > 0 && strcmp(ev->name, watch_file) == 0) {
                LOG_I("检测到文件变化 (mask=0x%08x)", ev->mask);
                triggered = 1;
            }
            ptr += sizeof(struct inotify_event) + ev->len;
        }
        if (triggered) {
            usleep(50 * 1000);
            int sw = read_switch();
            LOG_I("读取开关: %d (%s)", sw, sw ? "开启" : "关闭");
            apply_state(sw);
        }
    }

    inotify_rm_watch(ifd, wd);
    close(ifd);
    return;

poll_fallback:
    LOG_W("进入轮询模式（每 3 秒检查一次）");
    for (;;) {
        apply_state(read_switch());
        sleep(3);
    }
}

int main(void)
{
    log_open();
    LOG_I("===== Color墓碑完全体 服务启动 =====");
    LOG_I("开关文件: " SWITCH_FILE);
    LOG_I("日志文件: " LOG_FILE);

    wait_boot_completed();
    snapshot_original_props();

    if (init_cgroup_dirs() != 0) {
        LOG_E("cgroup 初始化失败，进程退出");
        log_close();
        return 1;
    }

    int initial = read_switch();
    LOG_I("初始开关: %d (%s)", initial, initial ? "开启" : "关闭");
    apply_state(initial);

    watch_loop();

    log_close();
    return 0;
}
