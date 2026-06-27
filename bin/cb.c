#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <poll.h>
#include <errno.h>
#include <sys/mount.h>

#define LEVEL_PATH      "/sys/class/power_supply/battery/capacity"
#define NODE_PATH       "/sys/devices/virtual/oplus_chg/battery/mmi_charging_enable"
#define USB_ONLINE_PATH "/sys/class/power_supply/usb/online"
#define BATT_LOG_PATH   "/sys/class/oplus_chg/battery/battery_log_content"

#define BASE_DIR    "/data/adb/modules/AAaTempSpoof/AaTempSpoof/"
#define SWITCH_PATH BASE_DIR "cb.txt"
#define CONFIG_PATH BASE_DIR "cbpz.txt"
#define LOG_PATH    BASE_DIR "tempspoof.log"
#define PID_PATH    "/data/adb/modules/AAaTempSpoof/pid/cb.pid"
#define BYPASS_PATH    "/data/adb/modules/AAaTempSpoof/pid/旁路"
#define CHGSPOOF_FLAG  "/data/adb/modules/AAaTempSpoof/pid/充电状态伪装"
#define BATT_STATUS_PATH "/sys/class/power_supply/battery/status"
#define FAKE_STATUS_PATH "/data/adb/modules/AAaTempSpoof/pid/fake_status"

#define CHG_TYPE_SVOOC 14
#define CHG_TYPE_UFCS  15

#define LOOP_DELAY 5

static int g_pid_fd = -1;

int acquire_single_instance(void) {
    g_pid_fd = open(PID_PATH, O_RDWR | O_CREAT, 0644);
    if (g_pid_fd < 0) return 0;
    struct flock fl;
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    if (fcntl(g_pid_fd, F_SETLK, &fl) < 0) {
        close(g_pid_fd);
        g_pid_fd = -1;
        return 0;
    }
    ftruncate(g_pid_fd, 0);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", (int)getpid());
    write(g_pid_fd, pid_str, strlen(pid_str));
    return 1;
}

int level_limit  = 80;
int trigger_time = 180;

#define CB_LOG_MAX_BYTES (512 * 1024)

void log_debug(const char *tag, const char *msg) {
    struct stat _st;
    if (stat(LOG_PATH, &_st) == 0 && _st.st_size > CB_LOG_MAX_BYTES) {
        char bak[256];
        snprintf(bak, sizeof(bak), "%s.bak", LOG_PATH);
        rename(LOG_PATH, bak);
    }
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *tm_p = localtime(&t);
    fprintf(f, "[%02d-%02d %02d:%02d:%02d] [%s] %s\n",
            tm_p->tm_mon + 1, tm_p->tm_mday,
            tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec,
            tag, msg);
    fclose(f);
}

int read_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

int write_node(int v) {
    FILE *f = fopen(NODE_PATH, "w");
    if (!f) return 0;
    fprintf(f, "%d", v);
    fclose(f);
    return 1;
}

void load_config(void) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        log_debug("CONFIG", "错误: 无法打开配置文件 cbpz.txt，使用默认参数");
        return;
    }
    char line[128], log_buf[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "dl=", 3) == 0) {
            level_limit = atoi(line + 3);
            snprintf(log_buf, sizeof(log_buf), "设置电量阈值: %d%%", level_limit);
            log_debug("CONFIG", log_buf);
        }
        if (strncmp(line, "sj=", 3) == 0) {
            trigger_time = atoi(line + 3);
            snprintf(log_buf, sizeof(log_buf), "设置触发间隔: %d秒", trigger_time);
            log_debug("CONFIG", log_buf);
        }
    }
    fclose(f);
}

int is_charging_now(void) {
    return (read_int(USB_ONLINE_PATH) == 1);
}

int read_charge_type(void) {

    FILE *f = fopen(BATT_LOG_PATH, "r");
    if (!f) return -1;

    char line[2048];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);


    char *p = line;
    for (int idx = 0; idx < 9; idx++) {
        p = strchr(p, ',');
        if (!p) return -1;
        p++;
    }
    return atoi(p);
}

int bypass_file_exists(void) {
    struct stat st;
    return (stat(BYPASS_PATH, &st) == 0);
}

static int    g_status_bound    = 0;
static time_t g_spoof_window_end = 0;

static int in_spoof_window(void)
{
    if (g_spoof_window_end == 0) return 0;
    if (time(NULL) <= g_spoof_window_end) return 1;
    g_spoof_window_end = 0;
    return 0;
}

static void bind_batt_status(void)
{
    if (g_status_bound) return;
    struct stat _st;
    if (stat(BATT_STATUS_PATH, &_st) != 0) return;
    int fd = open(FAKE_STATUS_PATH, O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0644);
    if (fd < 0) return;
    const char *val = "Charging\n";
    write(fd, val, strlen(val));
    close(fd);
    if (mount(FAKE_STATUS_PATH, BATT_STATUS_PATH, NULL, MS_BIND, NULL) == 0)
        g_status_bound = 1;
    else
        unlink(FAKE_STATUS_PATH);
}

static void unbind_batt_status(void)
{
    if (!g_status_bound) return;
    umount2(BATT_STATUS_PATH, MNT_DETACH);
    g_status_bound = 0;
}

static int g_usbfd = -1, g_usbwd = -1;

static void status_spoof_init(void)
{
    g_usbfd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_usbfd >= 0) {
        g_usbwd = inotify_add_watch(g_usbfd,
                      "/sys/class/power_supply/usb", IN_MODIFY);
        if (g_usbwd < 0) { close(g_usbfd); g_usbfd = -1; }
    }
}

static void status_spoof_cleanup(void)
{
    unbind_batt_status();
    if (g_usbfd >= 0) {
        if (g_usbwd >= 0) inotify_rm_watch(g_usbfd, g_usbwd);
        close(g_usbfd);
        g_usbfd = -1; g_usbwd = -1;
    }
}

static void update_batt_status_spoof(void)
{

    struct stat _st1;
    int has_spoof_flag = (stat(CHGSPOOF_FLAG, &_st1) == 0);


    int has_bypass = bypass_file_exists();


    if (!has_spoof_flag || has_bypass || !in_spoof_window()) {
        if (g_status_bound) unbind_batt_status();
        return;
    }


    if (!g_status_bound) bind_batt_status();
}

static void status_spoof_on_usb_event(void)
{
    if (g_usbfd < 0) return;
    char eb[256];
    while (read(g_usbfd, eb, sizeof(eb)) > 0) {}
    update_batt_status_spoof();
}

void bypass_guard(void)
{

    if (!write_node(0)) {
        log_debug("BYPASS", "ERROR: 写入 mmi_charging_enable=0 失败，退出守护");
        return;
    }
    log_debug("BYPASS", "旁路守护启动，mmi_charging_enable → 0");


    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int iwd = -1;
    if (ifd >= 0) {


        iwd = inotify_add_watch(ifd,
                  "/sys/devices/virtual/oplus_chg/battery",
                  IN_MODIFY);
        if (iwd < 0) {
            close(ifd);
            ifd = -1;
        }
    }

    struct pollfd pfd;
    pfd.fd     = ifd;
    pfd.events = POLLIN;

    while (1) {

        if (!is_charging_now()) {
            log_debug("BYPASS", "充电停止，旁路守护退出，mmi_charging_enable → 1");
            write_node(1);
            break;
        }
        if (!bypass_file_exists()) {
            log_debug("BYPASS", "旁路文件消失，守护退出，mmi_charging_enable → 1");
            write_node(1);
            break;
        }


        int ret = poll(&pfd, (ifd >= 0) ? 1 : 0,
                       LOOP_DELAY * 1000);

        if (ret > 0 && (pfd.revents & POLLIN)) {

            char eb[512];
            while (read(ifd, eb, sizeof(eb)) > 0) {}


            int cur = read_int(NODE_PATH);
            if (cur != 0) {
                write_node(0);
                log_debug("BYPASS", "检测到外部修改，mmi_charging_enable 强制写回 0");
            }
        }


    }

    if (ifd >= 0) {
        if (iwd >= 0) inotify_rm_watch(ifd, iwd);
        close(ifd);
    }
}

int main(void) {
    if (!acquire_single_instance()) {
        return 0;
    }

    log_debug("SYSTEM", "程序启动，开始监听...");
    load_config();
    status_spoof_init();

    time_t last_trigger        = time(NULL);
    int    config_reload_counter = 0;


    int prev_cb_mode     = -1;
    int prev_charging    = -1;
    int prev_charge_type = -2;
    int prev_level       = -1;

    while (1) {

        if (g_usbfd >= 0) {
            struct pollfd _upfd = { g_usbfd, POLLIN, 0 };
            if (poll(&_upfd, 1, 0) > 0 && (_upfd.revents & POLLIN))
                status_spoof_on_usb_event();
        }


        int charging = is_charging_now();
        int level    = read_int(LEVEL_PATH);
        if (level < 0) level = 0;

        if (charging != prev_charging) {
            int old_charging = prev_charging;
            char buf[64];
            snprintf(buf, sizeof(buf), "充电状态变化: usb/online=%d (电量:%d%%)",
                     charging, level);
            log_debug("STATE", buf);
            prev_charging = charging;
            if (old_charging == 0 && charging == 1) {
                last_trigger = time(NULL);
                g_spoof_window_end = 0;
                config_reload_counter = 0;
                prev_charge_type = -2;
                update_batt_status_spoof();
                log_debug("ACTION", "刚插上电，本轮从当前时间重新计时，避免立即触发伪插拔");
            } else if (old_charging == 1 && charging == 0) {
                last_trigger = time(NULL);
                g_spoof_window_end = 0;
                prev_charge_type = -2;
                update_batt_status_spoof();
                log_debug("ACTION", "充电断开，重置插拔计时并关闭伪装窗口");
            }
        }


        if (prev_level < 0 || level / 5 != prev_level / 5) {
            char buf[64];
            snprintf(buf, sizeof(buf), "当前电量: %d%%", level);
            log_debug("STATE", buf);
            prev_level = level;
        }




        if (charging && bypass_file_exists()) {
            log_debug("BYPASS", "充电中检测到旁路文件，进入旁路守护模式");
            bypass_guard();

            last_trigger = time(NULL);
            config_reload_counter = 0;
            prev_charge_type = -2;
            sleep(1);
            continue;
        }


        int cb_mode = read_int(SWITCH_PATH);
        if (cb_mode != 1 && cb_mode != 2) {
            if (cb_mode != prev_cb_mode) {
                log_debug("SWITCH", "cb.txt != 1/2，插拔功能已关闭");
                prev_cb_mode = cb_mode;
            }
            sleep(10);
            continue;
        }
        if (cb_mode != prev_cb_mode) {
            char buf[64];
            snprintf(buf, sizeof(buf), "模式切换: cb_mode=%d (%s)",
                cb_mode, cb_mode == 1 ? "不限协议" : "仅SVOOC/UFCS");
            log_debug("SWITCH", buf);
            prev_cb_mode = cb_mode;
        }


        if (config_reload_counter % 6 == 0) {
            load_config();
        }


        if (charging && level < level_limit) {

            if (cb_mode == 2) {
                int ct = read_charge_type();

                if (ct != prev_charge_type) {
                    char buf[128];
                    const char *proto_str;
                    if      (ct == CHG_TYPE_SVOOC) proto_str = "SVOOC(14) ✓";
                    else if (ct == CHG_TYPE_UFCS)  proto_str = "UFCS(15)  ✓";
                    else if (ct < 0)               proto_str = "无法读取协议";
                    else                            proto_str = "非SVOOC/UFCS，跳过";
                    char buf2[128];
                    snprintf(buf2, sizeof(buf2), "协议变化: charge_type=%d → %s",
                             ct, proto_str);
                    log_debug("PROTO", buf2);
                    prev_charge_type = ct;
                }

                if (ct != CHG_TYPE_SVOOC && ct != CHG_TYPE_UFCS) {
                    config_reload_counter++;
                    sleep(LOOP_DELAY);
                    continue;
                }
            }


            time_t now  = time(NULL);
            long   diff = (long)(now - last_trigger);



            long pre_window = (trigger_time > 30) ? (trigger_time - 30) : 0;
            if (diff >= pre_window && g_spoof_window_end == 0) {

                g_spoof_window_end = last_trigger + (time_t)trigger_time + 32;
                log_debug("SPOOF", "进入插拔前伪装窗口（插拔前≤30s）");
                update_batt_status_spoof();
            }

            if (diff >= trigger_time) {
                char act_log[128];
                snprintf(act_log, sizeof(act_log),
                         "执行伪插拔! 当前:%d%%, 间隔已达:%lds", level, diff);
                log_debug("ACTION", act_log);

                if (write_node(0)) {
                    sleep(2);
                    write_node(1);
                    last_trigger = now;

                    g_spoof_window_end = now + 30;
                    log_debug("ACTION", "插拔完成，伪装保持30秒后自动结束");
                } else {
                    log_debug("ERROR", "写入节点失败，请检查Root权限或内核路径");
                }
            }
        }

        update_batt_status_spoof();
        config_reload_counter++;
        sleep(LOOP_DELAY);
    }
    status_spoof_cleanup();
    return 0;
}
