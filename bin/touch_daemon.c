

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
#include <sys/wait.h>
#include <sys/file.h>
#include <poll.h>

#define BASE        "/data/adb/modules/AAaTempSpoof/AaTempSpoof"
#define CONFIG_FILE BASE "/touch_opt.conf"
#define LOG_FILE    BASE "/tempspoof.log"
#define PID_FILE    "/data/adb/modules/AAaTempSpoof/pid/touch_daemon.pid"

#define LOG_MAX     (512 * 1024)
#define CMD         "touchHidlTest"

typedef struct {
    int   rate;
    int   reg;
    const char *val;
    const char *label;
} RateEntry;

static const RateEntry RATES[] = {
    { 125, 26,  "0",   "125Hz (reg26=0x0)"   },
    { 240, 26,  "1",   "240Hz (reg26=0x1)"   },
    { 241, 182, "240", "241Hz (reg182=0xf0)"  },
    { 360, 26,  "12c", "360Hz (reg26=0x12c)"  },
    { 361, 26,  "c",   "361Hz (reg26=0xc)"    },
    { 362, 182, "360", "362Hz (reg182=0x168)" },
    { 600, 26,  "258", "600Hz (reg26=0x258)"  },
};
#define RATES_LEN (int)(sizeof(RATES) / sizeof(RATES[0]))

static volatile int g_quit = 0;
static int g_pid_fd = -1;

static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
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
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [touch] %s\n",
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

static int read_global_rate(void)
{
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return 0;

    char line[256];
    int rate = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "global_sample_rate=", 19) == 0) {
            rate = atoi(line + 19);
            break;
        }
    }
    fclose(f);
    return rate;
}

static const RateEntry *find_rate(int rate)
{
    for (int i = 0; i < RATES_LEN; i++) {
        if (RATES[i].rate == rate) return &RATES[i];
    }
    return NULL;
}

static int apply_rate(const RateEntry *e)
{

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s -c wo 0 %d %s >/dev/null 2>&1",
             CMD, e->reg, e->val);

    int ret = system(cmd);
    if (ret != 0) return -1;
    return 0;
}

int main(void)
{

    if (acquire_pid_lock() < 0) {

        return 0;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);
    signal(SIGCHLD, SIG_DFL);

    do_log("守护进程已启动");


    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd < 0) {
        do_log("inotify_init 失败，退出");
        release_pid_lock();
        return 1;
    }

    int wd = -1;

    wd = inotify_add_watch(ifd, BASE,
                           IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);


    int current_rate = -1;

    int rate = read_global_rate();
    const RateEntry *e = find_rate(rate);

    if (e) {
        if (apply_rate(e) == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "已应用 %s", e->label);
            do_log(msg);
            current_rate = rate;
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "应用 %dHz 失败", rate);
            do_log(msg);
        }
    } else if (rate != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "不支持的采样率: %d", rate);
        do_log(msg);
    } else {

        do_log("全局采样率为0，不干预系统");
        current_rate = 0;
    }


    char buf[sizeof(struct inotify_event) + 256] __attribute__((aligned(8)));
    struct pollfd pfd = { .fd = ifd, .events = POLLIN };

    while (!g_quit) {
        int ret = poll(&pfd, 1, 3000);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {

            if (current_rate > 0) {
                const RateEntry *ce = find_rate(current_rate);
                if (ce) apply_rate(ce);
            }
            continue;
        }

        ssize_t len = read(ifd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) continue;
            break;
        }


        int config_changed = 0;
        char *p = buf;
        while (p < buf + len) {
            struct inotify_event *ev = (struct inotify_event *)p;
            if (ev->len > 0) {
                if (strncmp(ev->name, "touch_opt.conf", 14) == 0) {
                    config_changed = 1;
                }
            }

            if (ev->mask & IN_IGNORED) {
                wd = inotify_add_watch(ifd, BASE,
                                       IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
            }
            p += sizeof(struct inotify_event) + ev->len;
        }

        if (!config_changed) continue;


        int new_rate = read_global_rate();
        if (new_rate == current_rate) continue;

        const RateEntry *ne = find_rate(new_rate);
        if (ne) {
            if (apply_rate(ne) == 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "切换: %dHz -> %s",
                         current_rate, ne->label);
                do_log(msg);
                current_rate = new_rate;
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "应用 %dHz 失败", new_rate);
                do_log(msg);
            }
        } else if (new_rate == 0) {
            do_log("全局采样率切换为0，停止干预系统");
            current_rate = 0;
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "不支持的采样率: %d，忽略", new_rate);
            do_log(msg);
        }
    }

    if (wd >= 0) inotify_rm_watch(ifd, wd);
    close(ifd);

    do_log("守护进程已退出");
    release_pid_lock();
    return 0;
}
