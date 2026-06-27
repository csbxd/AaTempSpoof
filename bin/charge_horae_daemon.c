

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/file.h>
#include <poll.h>

#define BASE            "/data/adb/modules/AAaTempSpoof/AaTempSpoof"
#define LOG_FILE        BASE "/tempspoof.log"
#define PID_FILE        "/data/adb/modules/AAaTempSpoof/pid/charge_horae_daemon.pid"
#define PID_DIR         "/data/adb/modules/AAaTempSpoof/pid"

#define HORAE_FILE      BASE "/horae.txt"
#define BATTERY_STATUS  "/sys/class/power_supply/battery/status"

#define WZBAI_FILE      BASE "/wzbai.txt"
#define SWITCH_FILE     BASE "/总开关.txt"

#define PID_HORAE_CHARGE  PID_DIR "/充电禁用horae"

#define TASKS_DIR       "/data/system/recent_tasks"

#define LOG_MAX         (512 * 1024)
#define CHARGE_POLL_MS  2000

#define MAX_PKG         128
#define PKG_LEN         128

static volatile int g_quit              = 0;
static int          g_pid_fd            = -1;

static int          g_charging          = 0;
static char         g_horae_default[32] = "0";
static int          g_horae_forced      = 0;

static char         g_wzbai[MAX_PKG][PKG_LEN];
static int          g_wzbai_count       = 0;
static int          g_wzbai_exists      = 0;

static int          g_switch_val        = -1;

static long long    g_last_switch_ms       = 0;
#define SWITCH_DEBOUNCE_MS  1000

static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void do_log(const char *msg)
{
    struct stat st;
    if (stat(LOG_FILE, &st) == 0 && st.st_size > LOG_MAX) {
        char bak[256];
        snprintf(bak, sizeof(bak), "%s.bak", LOG_FILE);
        rename(LOG_FILE, bak);
    }

    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [charge_horae] %s\n",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, msg);
    fclose(f);
}

static int acquire_pid_lock(void)
{
    g_pid_fd = open(PID_FILE, O_RDWR | O_CREAT, 0644);
    if (g_pid_fd < 0) return -1;

    if (flock(g_pid_fd, LOCK_EX | LOCK_NB) < 0) {
        close(g_pid_fd);
        g_pid_fd = -1;
        return -1;
    }

    char buf[32];
    ftruncate(g_pid_fd, 0);
    lseek(g_pid_fd, 0, SEEK_SET);
    int n = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    write(g_pid_fd, buf, n);
    return 0;
}

static void release_pid_lock(void)
{
    if (g_pid_fd >= 0) {
        flock(g_pid_fd, LOCK_UN);
        close(g_pid_fd);
        g_pid_fd = -1;
    }
    unlink(PID_FILE);
}

static void read_horae(char *out, int out_len)
{
    FILE *f = fopen(HORAE_FILE, "r");
    if (!f) { strncpy(out, "0", out_len); return; }
    if (!fgets(out, out_len, f)) strncpy(out, "0", out_len);
    fclose(f);
    int n = (int)strlen(out);
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' '))
        out[--n] = '\0';
}

static void write_horae(const char *val)
{
    FILE *f = fopen(HORAE_FILE, "w");
    if (!f) return;
    fprintf(f, "%s\n", val);
    fclose(f);
}

static int read_charging(void)
{
    FILE *f = fopen(BATTERY_STATUS, "r");
    if (!f) return 0;
    char status[32] = {0};
    fgets(status, sizeof(status), f);
    fclose(f);
    if (strncmp(status, "Charging", 8) == 0 ||
        strncmp(status, "Full",     4) == 0)
        return 1;
    return 0;
}

static void handle_horae_charge(void)
{
    int pid_exists   = (access(PID_HORAE_CHARGE, F_OK) == 0);
    int should_force = g_charging && pid_exists;

    if (should_force && !g_horae_forced) {
        read_horae(g_horae_default, sizeof(g_horae_default));
        char snap_msg[80];
        snprintf(snap_msg, sizeof(snap_msg),
                 "充电禁用horae已存在，快照 horae 默认值: %s", g_horae_default);
        do_log(snap_msg);
        write_horae("1");
        g_horae_forced = 1;
        do_log("充电+充电禁用horae，horae.txt 已设为 1");
    } else if (!should_force && g_horae_forced) {
        write_horae(g_horae_default);
        g_horae_forced = 0;
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "条件解除，horae.txt 已恢复为 %s", g_horae_default);
        do_log(msg);
    }
}

static void handle_charging(int new_charging)
{
    if (new_charging == g_charging) return;
    g_charging = new_charging;

    if (new_charging)
        do_log("检测到充电");
    else
        do_log("充电结束");


    handle_horae_charge();
}

static void read_wzbai(void)
{
    g_wzbai_count = 0;
    FILE *f = fopen(WZBAI_FILE, "r");
    if (!f) {
        g_wzbai_exists = 0;
        return;
    }

    char line[PKG_LEN];
    while (fgets(line, sizeof(line), f) && g_wzbai_count < MAX_PKG) {
        int n = (int)strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' ||
                         line[n-1] == ' '  || line[n-1] == '\t'))
            line[--n] = '\0';
        if (n > 0) {
            strncpy(g_wzbai[g_wzbai_count], line, PKG_LEN - 1);
            g_wzbai[g_wzbai_count][PKG_LEN - 1] = '\0';
            g_wzbai_count++;
        }
    }
    fclose(f);

    g_wzbai_exists = 1;
    char msg[64];
    snprintf(msg, sizeof(msg), "wzbai.txt 已加载 %d 条包名", g_wzbai_count);
    do_log(msg);
}

static int in_whitelist(const char *pkg)
{
    for (int i = 0; i < g_wzbai_count; i++) {
        if (strcmp(g_wzbai[i], pkg) == 0) return 1;
    }
    return 0;
}

static void write_main_switch(int val)
{
    if (val == g_switch_val) return;

    FILE *f = fopen(SWITCH_FILE, "w");
    if (!f) return;
    fprintf(f, "%d\n", val);
    fclose(f);
    g_switch_val = val;

    char msg[64];
    snprintf(msg, sizeof(msg), "总开关.txt -> %d", val);
    do_log(msg);
}

static int parse_pkg_from_focus(const char *line, char *out, int out_len)
{
    const char *p = line;
    while (*p) {
        if (*p == ' ' && *(p + 1) == 'u') {
            const char *q = p + 2;
            while (*q >= '0' && *q <= '9') q++;
            if (q > p + 2 && *q == ' ') {
                q++;
                const char *slash = strchr(q, '/');
                const char *brace = strchr(q, '}');
                const char *end   = slash ? slash : brace;
                if (!end) end = q + strlen(q);
                int len = (int)(end - q);
                if (len > 0 && len < out_len) {
                    strncpy(out, q, len);
                    out[len] = '\0';
                    return 0;
                }
            }
        }
        p++;
    }
    return -1;
}

static int get_foreground_pkg(char *out, int out_len)
{
    FILE *fp = popen(
        "dumpsys window 2>/dev/null | grep -m1 'mCurrentFocus'", "r");
    if (!fp) return -1;

    char line[512] = {0};
    fgets(line, sizeof(line), fp);
    pclose(fp);

    if (strstr(line, "null")) return -1;

    return parse_pkg_from_focus(line, out, out_len);
}

static void check_and_update_switch(void)
{

    long long now = now_ms();
    if (now - g_last_switch_ms < SWITCH_DEBOUNCE_MS) return;
    g_last_switch_ms = now;


    if (!g_wzbai_exists) {
        write_main_switch(1);
        return;
    }

    char pkg[PKG_LEN] = {0};

    if (get_foreground_pkg(pkg, sizeof(pkg)) < 0) {

        write_main_switch(0);
        return;
    }

    write_main_switch(in_whitelist(pkg) ? 1 : 0);
}

int main(void)
{
    if (acquire_pid_lock() < 0) {
        return 0;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    do_log("守护进程已启动");


    read_horae(g_horae_default, sizeof(g_horae_default));
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "horae 用户默认值: %s", g_horae_default);
        do_log(msg);
    }
    g_charging = read_charging();
    if (g_charging) do_log("启动时已在充电");


    handle_horae_charge();


    read_wzbai();
    check_and_update_switch();


    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd < 0) {
        do_log("inotify_init 失败，退出");
        release_pid_lock();
        return 1;
    }


    int wd_base = inotify_add_watch(ifd, BASE,
                                    IN_CLOSE_WRITE | IN_MOVED_TO |
                                    IN_CREATE | IN_DELETE);



    int wd_tasks = inotify_add_watch(ifd, TASKS_DIR,
                                     IN_CREATE | IN_DELETE | IN_MOVED_TO);
    if (wd_tasks < 0) {
        do_log("recent_tasks inotify 不可用，将以轮询方式检测前台应用");
    } else {
        do_log("已启用 recent_tasks 实时前台应用检测");
    }



    int wd_pid = inotify_add_watch(ifd, PID_DIR,
                                   IN_CREATE | IN_DELETE | IN_MOVED_TO);
    if (wd_pid < 0) {
        do_log("pid 目录 inotify 不可用，将依赖充电轮询触发检查");
    } else {
        do_log("已启用 pid 目录实时监测（充电禁用horae）");
    }


    char buf[(sizeof(struct inotify_event) + 256) * 8]
        __attribute__((aligned(8)));
    struct pollfd pfd = { .fd = ifd, .events = POLLIN };

    while (!g_quit) {
        int ret = poll(&pfd, 1, CHARGE_POLL_MS);


        handle_charging(read_charging());

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) {

            if (wd_tasks < 0) check_and_update_switch();
            continue;
        }


        ssize_t len = read(ifd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) continue;
            break;
        }

        int wzbai_changed = 0;
        int tasks_changed = 0;
        int pid_changed   = 0;

        char *p = buf;
        while (p < buf + len) {
            struct inotify_event *ev = (struct inotify_event *)p;

            if (ev->wd == wd_base && ev->len > 0) {

                if (g_horae_forced &&
                    strncmp(ev->name, "horae.txt", 9) == 0) {
                    char cur[32];
                    read_horae(cur, sizeof(cur));
                    if (strcmp(cur, "1") != 0) {
                        write_horae("1");
                        do_log("强制期间检测到 horae.txt 被修改，已强制恢复为 1");
                    }
                }


                if (strncmp(ev->name, "wzbai.txt", 9) == 0) {
                    wzbai_changed = 1;
                }
            }

            if (ev->wd == wd_tasks) {

                tasks_changed = 1;
            }

            if (ev->wd == wd_pid && ev->len > 0) {

                if (strncmp(ev->name, "充电禁用horae",
                            strlen("充电禁用horae")) == 0) {
                    pid_changed = 1;
                }
            }


            if (ev->mask & IN_IGNORED) {
                if (ev->wd == wd_base) {
                    wd_base = inotify_add_watch(
                        ifd, BASE,
                        IN_CLOSE_WRITE | IN_MOVED_TO |
                        IN_CREATE | IN_DELETE);
                }
                if (ev->wd == wd_tasks) {
                    wd_tasks = inotify_add_watch(
                        ifd, TASKS_DIR,
                        IN_CREATE | IN_DELETE | IN_MOVED_TO);
                }
                if (ev->wd == wd_pid) {
                    wd_pid = inotify_add_watch(
                        ifd, PID_DIR,
                        IN_CREATE | IN_DELETE | IN_MOVED_TO);
                }
            }

            p += sizeof(struct inotify_event) + ev->len;
        }


        if (wzbai_changed) {
            read_wzbai();
            g_switch_val = -1;
            tasks_changed = 1;
        }


        if (tasks_changed) {
            check_and_update_switch();
        }


        if (pid_changed) {
            handle_horae_charge();
        }
    }

    if (wd_base  >= 0) inotify_rm_watch(ifd, wd_base);
    if (wd_tasks >= 0) inotify_rm_watch(ifd, wd_tasks);
    if (wd_pid   >= 0) inotify_rm_watch(ifd, wd_pid);
    close(ifd);

    do_log("守护进程已退出");
    release_pid_lock();
    return 0;
}
