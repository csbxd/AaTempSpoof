#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sched.h>
#include <stdarg.h>

#include "d1x00_data.h"

static int  read_horae_switch(void);
static void unbind_cap(void);
static void apply_horae_whitelist(void);
static void refresh_horae_policy(void);
static int  check_cdz_file(void);
static int  charger_online(void);
static void integrity_log(const char *fmt, ...);

#define VERSION       "13.5"
#define MAX_PATH      512
#define MAX_ZONES     256
#define FIXED_MV      36000
#define BATT_SYS      "/sys/class/power_supply/battery"
#define CFG_NAME "AaTempSpoof.txt"
#define DATA_DIR "/data/adb/modules/AAaTempSpoof/AaTempSpoof"
#define PID_DIR  "/data/adb/modules/AAaTempSpoof/pid"
#define COMPAT_MODE_FILE PID_DIR "/\xe5\x85\xbc\xe5\xae\xb9\xe6\xa8\xa1\xe5\xbc\x8f"
#define FAKE_EMPTY    PID_DIR "/fake_empty"
#define MAX_TC_MOUNTS 48


#define AATS_ROOT    "/data/adb/modules/AAaTempSpoof"
#define MODULE_PROP  AATS_ROOT "/module.prop"
#define INDEX_HTML   AATS_ROOT "/webroot/index.html"
#define UNINSTALL_SH AATS_ROOT "/uninstall.sh"
#define MODULE_DISABLE_FLAG AATS_ROOT "/disable"
#define MODULE_REMOVE_FLAG  AATS_ROOT "/remove"

static char g_tc_mounted[MAX_TC_MOUNTS][MAX_PATH];
static int  g_tc_nmounted = 0;

#define Z_CPU    0
#define Z_GPU    1
#define Z_MEM    2
#define Z_BATT   3
#define Z_PERIPH 4
#define Z_MISC   5
#define Z_SHELL  6

static const char *OPLUS_TEMP_NODES[] = {
    "/sys/class/oplus_chg/battery/temp",
    "/sys/class/oplus_chg/battery/batt_temp",
    "/sys/class/oplus_chg/battery/temp_level",
    "/sys/devices/platform/soc/soc:oplus,mms_gauge/oplus_mms/gauge/battery/temp",
    "/sys/class/xm_power/fg_master/temp",
    "/sys/devices/virtual/xm_power/fg_master/temp",
    NULL
};

static volatile sig_atomic_t g_run    = 1;
static volatile sig_atomic_t g_reload = 0;
static int g_module_restore_started = 0;

static char g_cfgpath   [MAX_PATH];
static char g_pidpath   [MAX_PATH];
static char g_fakepath  [MAX_PATH];
static char g_fakeuevpath [MAX_PATH];
static char g_fakebccpath [MAX_PATH];
static char g_fakecyclepath[MAX_PATH];
static char g_fakecappath  [MAX_PATH];
static char g_masterpath   [MAX_PATH];
static char g_horaespath   [MAX_PATH];
static char g_horaebai_path[MAX_PATH];
static char g_cdmspath     [MAX_PATH];
static char g_cdzpath      [MAX_PATH];
static char g_jzwzpath     [MAX_PATH];
static char g_blpath       [MAX_PATH];

static int  g_master        = 1;
static int  g_master_prev   = -1;

static int  g_horae_sw      = 1;
static int  g_horae_sw_prev = -1;
static int  g_horae_apply_last = -1;

static int  g_cdms          = 0;
static int  g_cdms_prev     = -1;
static int  g_cdz_active    = 0;
static int  g_cdz_prev      = -1;

static int  g_jzwz          = 0;
static int  g_jzwz_prev     = -1;
static int  g_rf_emul_delay = 0;

static int  g_uev_bound   = 0;
static int  g_real_uev_fd = -1;
static int  g_bcc_bound   = 0;
static int  g_real_bcc_fd = -1;
static int  g_cycle_bound = 0;
static int  g_cap_bound   = 0;
static int  g_real_cap_fd = -1;
static int  g_chip_soc_bound = 0;
static int  g_vooc_handshake_guard = 0;
static int  g_skip_dumpsys_set = 0;
static int  g_shell_val = -1;

static int g_cpu_t      = 400;
static int g_gpu_t      = 400;
static int g_mem_t      = 400;
static int g_batt_t     = 360;
static int g_oth_t      = 360;
static int g_batt_cycle = 100;

static int g_cpu_lo  = 400, g_cpu_hi  = 400;
static int g_gpu_lo  = 400, g_gpu_hi  = 400;
static int g_mem_lo  = 400, g_mem_hi  = 400;
static int g_batt_lo = 360, g_batt_hi = 360;
static int g_oth_lo  = 360, g_oth_hi  = 360;


static int g_batt_charge_start_t = 350;
static int g_batt_charge_mid_t   = 385;
static int g_batt_charge_stop_t  = 420;
static int g_batt_charge_minutes = 0;
static int g_batt_charge_active  = 0;
static long long g_batt_charge_start_ms = 0;


#define DYN_ROLL_INTERVAL_MS 1000LL

static int g_en_cpu  = 1;
static int g_en_gpu  = 1;
static int g_en_mem  = 1;
static int g_en_batt = 1;
static int g_en_oth  = 1;
static int g_en_cycle= 1;
static int g_en_cap      = 0;
static int g_fake_cap    = 50;
static int g_en_supersave= 0;
static int g_en_gpu_mem_trip_wall = 1;

#define MAX_TRIPS 10
typedef struct {
    char path[MAX_PATH];
    char mode[MAX_PATH];
    int  type;
    int  rf;
    int  trip_orig[MAX_TRIPS];
    int  last_emul;
} Zone;
static Zone g_zones[MAX_ZONES];
static int  g_nzones     = 0;
static int  g_batt_bound = 0;

static void on_term(int s) { g_run    = 0; (void)s; }
static void on_hup (int s) { g_reload = 1; (void)s; }

static void wr_int(const char *path, int v)
{
    int fd = open(path, O_WRONLY|O_CLOEXEC); if (fd < 0) return;
    char buf[32]; int n = snprintf(buf, sizeof(buf), "%d\n", v);
    (void)write(fd, buf, n); close(fd);
}
static void wr_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY|O_CLOEXEC); if (fd < 0) return;
    (void)write(fd, s, strlen(s)); close(fd);
}
static int rd_int(const char *path)
{
    int fd = open(path, O_RDONLY|O_CLOEXEC); if (fd < 0) return -1;
    char buf[32]; int n = (int)read(fd, buf, 31); close(fd);
    if (n <= 0) return -1; buf[n] = '\0'; return atoi(buf);
}
static int charge_nodes_enabled(void)
{
    return access(COMPAT_MODE_FILE, F_OK) != 0;
}
static void unmount_all(const char *path)
{
    while (umount2(path, MNT_DETACH) == 0) {}
}
static void write_shell_temp(int val)
{
    (void)system("dumpsys horae testmode >/dev/null 2>&1");
    int fd = open("/proc/shell-temp", O_WRONLY|O_CLOEXEC); if (fd < 0) return;
    for (int i = 0; i <= 9; i++) {
        char b[24]; int n = snprintf(b, sizeof(b), "%d %d\n", i, val);
        (void)write(fd, b, n);
    }
    close(fd);
}

static int parse_temp_deci(const char *s, int *out);

static int rd_temp_deci(const char *path)
{
    int fd = open(path, O_RDONLY|O_CLOEXEC); if (fd < 0) return -1;
    char buf[32]; int n = (int)read(fd, buf, 31); close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    int v;
    return parse_temp_deci(buf, &v) ? v : -1;
}

static void run_async(const char *cmd)
{
    pid_t p = fork();
    if (p < 0) return;
    if (p == 0) {
        setsid();
        pid_t g = fork();
        if (g < 0) _exit(1);
        if (g > 0) _exit(0);
        execl("/system/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }
    int st; waitpid(p, &st, 0);
}

static void run_sync(const char *cmd)
{
    pid_t p = fork();
    if (p < 0) return;
    if (p == 0) {
        execl("/system/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }
    int st;
    while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
}

static void module_restore_notify(void)
{
    run_async(
        "su 2000 -c \"cmd notification post -S bigtext -t \\\"AaTempSpoof\\\" "
        "tempspoof_action \\\""
        "\xe6\xa3\x80\xe6\xb5\x8b\xe5\x88\xb0\xe6\xa8\xa1\xe5\x9d\x97"
        "\xe5\xb7\xb2\xe5\x85\xb3\xe9\x97\xad\xe6\x88\x96\xe5\x8d\xb8\xe8\xbd\xbd"
        "\xef\xbc\x8c\xe6\xad\xa3\xe5\x9c\xa8\xe6\x89\xa7\xe8\xa1\x8c"
        "\xe6\x81\xa2\xe5\xa4\x8d\xe8\x84\x9a\xe6\x9c\xac"
        "\\\"\" 2>/dev/null"
    );
}

static int module_restore_requested(void)
{
    return access(MODULE_DISABLE_FLAG, F_OK) == 0 ||
           access(MODULE_REMOVE_FLAG,  F_OK) == 0;
}

static void module_restore_response(void)
{
    if (g_module_restore_started) return;
    g_module_restore_started = 1;

    integrity_log("module disable/remove detected | disable=%d remove=%d | execute uninstall.sh",
                  access(MODULE_DISABLE_FLAG, F_OK) == 0,
                  access(MODULE_REMOVE_FLAG,  F_OK) == 0);
    module_restore_notify();
    run_async("/system/bin/sh " UNINSTALL_SH " 2>/dev/null");
    g_run = 0;
}

static void mkdirp(const char *path)
{
    char tmp[MAX_PATH]; strncpy(tmp, path, MAX_PATH-1);
    for (char *p = tmp+1; *p; p++)
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}
static int write_blob(const char *path, const unsigned char *data, size_t len)
{
    mkdirp(path); FILE *f = fopen(path, "wb"); if (!f) return -1;
    fwrite(data, 1, len, f); fclose(f); return 0;
}

static int write_file_if_changed(const char *path, const char *data, size_t len, mode_t mode)
{
    int fd = open(path, O_RDONLY|O_CLOEXEC);
    if (fd >= 0) {
        char buf[512];
        size_t off = 0;
        int same = 1;
        for (;;) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n < 0) { same = 0; break; }
            if (n == 0) break;
            if (off + (size_t)n > len || memcmp(buf, data + off, (size_t)n) != 0) {
                same = 0;
                break;
            }
            off += (size_t)n;
        }
        close(fd);
        if (same && off == len) return 0;
    }

    fd = open(path, O_WRONLY|O_CREAT|O_CLOEXEC, mode);
    if (fd < 0) return -1;
    if (lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) { close(fd); return -1; }
        off += (size_t)n;
    }
    ftruncate(fd, (off_t)len);
    close(fd);
    return 1;
}

static int temp_to_millic(int deci)
{
    return deci * 100;
}

static int temp_to_deci(int deci)
{
    return deci;
}

static int parse_temp_deci(const char *s, int *out)
{
    while ((unsigned char)*s <= ' ' && *s) s++;
    char *end = NULL;
    long whole = strtol(s, &end, 10);
    if (end == s || whole < 0) return 0;

    int frac = 0;
    if (*end == '.') {
        end++;
        if (*end >= '0' && *end <= '9') frac = *end - '0';
        while (*end >= '0' && *end <= '9') end++;
    }
    while ((unsigned char)*end <= ' ' && *end) end++;
    if (*end != '\0' && *end != '-') return 0;

    *out = (int)(whole * 10 + frac);
    return 1;
}

static int parse_temp_range(const char *v, int *lo, int *hi)
{
    int a;
    if (!parse_temp_deci(v, &a) || a < 0) return 0;

    const char *dash = strchr(v, '-');
    if (dash && dash != v) {
        int b;
        if (!parse_temp_deci(dash + 1, &b)) { *lo = *hi = a; return 1; }
        if (a <= b) { *lo = a; *hi = b; }
        else        { *lo = b; *hi = a; }
        return 1;
    }
    *lo = *hi = a;
    return 1;
}

static long long monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}


static int pick_temp(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo + 1);
}

static void reset_batt_charge_ramp(void)
{
    g_batt_charge_active = 0;
    g_batt_charge_start_ms = 0;
}

static int batt_charge_ramp_enabled(void)
{
    return g_en_batt &&
           g_batt_charge_minutes > 0 &&
           g_batt_charge_start_t > 0 &&
           g_batt_charge_mid_t > 0 &&
           g_batt_charge_stop_t > 0;
}

static void update_batt_charge_ramp(int charging)
{
    if (!charging || !batt_charge_ramp_enabled()) {
        reset_batt_charge_ramp();
        return;
    }

    long long now = monotonic_ms();
    if (now <= 0) {
        g_batt_t = g_batt_charge_start_t;
        return;
    }

    if (!g_batt_charge_active || g_batt_charge_start_ms <= 0) {
        g_batt_charge_active = 1;
        g_batt_charge_start_ms = now;
    }

    long long duration_ms = (long long)g_batt_charge_minutes * 60LL * 1000LL;
    long long elapsed_ms = now - g_batt_charge_start_ms;
    if (elapsed_ms < 0) elapsed_ms = 0;

    if (duration_ms <= 0 || elapsed_ms >= duration_ms) {
        g_batt_t = g_batt_charge_stop_t;
        return;
    }

    long long half_ms = duration_ms / 2;
    if (half_ms <= 0) {
        g_batt_t = g_batt_charge_stop_t;
        return;
    }

    if (elapsed_ms <= half_ms) {
        int span = g_batt_charge_mid_t - g_batt_charge_start_t;
        g_batt_t = g_batt_charge_start_t + (int)((long long)span * elapsed_ms / half_ms);
    } else {
        long long tail_ms = duration_ms - half_ms;
        long long tail_elapsed = elapsed_ms - half_ms;
        int span = g_batt_charge_stop_t - g_batt_charge_mid_t;
        g_batt_t = g_batt_charge_mid_t + (int)((long long)span * tail_elapsed / tail_ms);
    }
}

static void roll_dynamic_temps(void)
{
    int charging = charger_online();
    g_cpu_t  = pick_temp(g_cpu_lo,  g_cpu_hi);
    g_gpu_t  = pick_temp(g_gpu_lo,  g_gpu_hi);
    g_mem_t  = pick_temp(g_mem_lo,  g_mem_hi);
    if (charging && batt_charge_ramp_enabled())
        update_batt_charge_ramp(1);
    else {
        update_batt_charge_ramp(0);
        g_batt_t = pick_temp(g_batt_lo, g_batt_hi);
    }
    g_oth_t  = pick_temp(g_oth_lo,  g_oth_hi);
}

static void parse_config(void)
{
    int old_batt_charge_start = g_batt_charge_start_t;
    int old_batt_charge_mid = g_batt_charge_mid_t;
    int old_batt_charge_stop = g_batt_charge_stop_t;
    int old_batt_charge_minutes = g_batt_charge_minutes;
    FILE *f = fopen(g_cfgpath, "r"); if (!f) return;
    char line[256];
    int parsed_batt_charge_mid = 0;
    while (fgets(line, sizeof(line), f)) {
        char *cm = strchr(line, '#'); if (cm) *cm = '\0';
        int l = (int)strlen(line);
        while (l > 0 && (unsigned char)line[l-1] <= ' ') line[--l] = '\0';
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq+1;
        while ((unsigned char)*k <= ' ' && *k) k++;
        while ((unsigned char)*v <= ' ' && *v) v++;
        int kl = (int)strlen(k);
        while (kl > 0 && (unsigned char)k[kl-1] <= ' ') k[--kl] = '\0';

        if (!strcmp(k,"cpu\xe4\xbc\xaa\xe8\xa3\x85\xe5\xbc\x80\xe5\x85\xb3")||!strcmp(k,"cpu_en"))
            { g_en_cpu =(atoi(v)!=0); continue; }
        if (!strcmp(k,"gpu\xe4\xbc\xaa\xe8\xa3\x85\xe5\xbc\x80\xe5\x85\xb3")||!strcmp(k,"gpu_en"))
            { g_en_gpu =(atoi(v)!=0); continue; }
        if (!strcmp(k,"\xe5\x86\x85\xe5\xad\x98\xe4\xbc\xaa\xe8\xa3\x85\xe5\xbc\x80\xe5\x85\xb3")||!strcmp(k,"mem_en"))
            { g_en_mem =(atoi(v)!=0); continue; }
        if (!strcmp(k,"\xe7\x94\xb5\xe6\xb1\xa0\xe4\xbc\xaa\xe8\xa3\x85\xe5\xbc\x80\xe5\x85\xb3")||!strcmp(k,"batt_en"))
            { g_en_batt=(atoi(v)!=0); continue; }

        if (!strcmp(k,"\xe5\x85\xb6\xe4\xbb\x96\xe6\xb8\xa9\xe5\xba\xa6\xe4\xbc\xaa\xe8\xa3\x85")||!strcmp(k,"oth_en"))
            { g_en_oth =(atoi(v)!=0); continue; }

        if (!strcmp(k,"\xe7\x94\xb5\xe6\xb1\xa0\xe5\xbe\xaa\xe7\x8e\xaf\xe4\xbc\xaa\xe8\xa3\x85")||!strcmp(k,"cycle_en"))
            { g_en_cycle=(atoi(v)!=0); continue; }

        if (!strcmp(k,"\xe7\x94\xb5\xe9\x87\x8f\xe4\xbc\xaa\xe8\xa3\x85\xe5\xbc\x80\xe5\x85\xb3")||!strcmp(k,"cap_en"))
            { g_en_cap=(atoi(v)!=0); continue; }

        if (!strcmp(k,"\xe7\xa7\xbb\xe9\x99\xa4\xe8\xb6\x85\xe7\xba\xa7\xe7\x9c\x81\xe7\x94\xb5")||!strcmp(k,"supersave_en"))
            { g_en_supersave=(atoi(v)!=0); continue; }

        if (!strcmp(k,"\xe4\xbf\xae\xe6\x94\xb9\xe6\xb8\xa9\xe5\xba\xa6\xe5\xa2\x99")||!strcmp(k,"trip_wall_en"))
            { g_en_gpu_mem_trip_wall=(atoi(v)!=0); continue; }

        if      (!strcmp(k,"CPU\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"cpu_tmp"))
            parse_temp_range(v, &g_cpu_lo, &g_cpu_hi);
        else if (!strcmp(k,"GPU\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"gpu_tmp"))
            parse_temp_range(v, &g_gpu_lo, &g_gpu_hi);
        else if (!strcmp(k,"\xe5\x86\x85\xe5\xad\x98\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"mem_tmp"))
            parse_temp_range(v, &g_mem_lo, &g_mem_hi);
        else if (!strcmp(k,"\xe7\x94\xb5\xe6\xb1\xa0\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"batt_tmp"))
            parse_temp_range(v, &g_batt_lo, &g_batt_hi);
        else if (!strcmp(k,"\xe7\x94\xb5\xe6\xb1\xa0\xe5\x85\x85\xe7\x94\xb5\xe5\xbc\x80\xe5\xa7\x8b\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"batt_charge_start_tmp")) {
            int t;
            if (parse_temp_deci(v, &t) && t > 0) g_batt_charge_start_t = t;
        }
        else if (!strcmp(k,"\xe5\x85\x85\xe7\x94\xb5\xe4\xb8\xad\xe9\x97\xb4\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"batt_charge_mid_tmp")||!strcmp(k,"batt_charge_middle_tmp")) {
            int t;
            if (parse_temp_deci(v, &t) && t > 0) {
                g_batt_charge_mid_t = t;
                parsed_batt_charge_mid = 1;
            }
        }
        else if (!strcmp(k,"\xe7\x94\xb5\xe6\xb1\xa0\xe5\x85\x85\xe7\x94\xb5\xe6\x88\xaa\xe6\xad\xa2\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"batt_charge_stop_tmp")) {
            int t;
            if (parse_temp_deci(v, &t) && t > 0) g_batt_charge_stop_t = t;
        }
        else if (!strcmp(k,"\xe5\x85\x85\xe7\x94\xb5\xe4\xbc\xaa\xe8\xa3\x85\xe6\x97\xb6\xe9\x97\xb4")||!strcmp(k,"batt_charge_time")) {
            int minutes = atoi(v);
            g_batt_charge_minutes = minutes > 0 ? minutes : 0;
        }
        else if (!strcmp(k,"\xe5\x85\xb6\xe4\xbb\x96\xe6\xb8\xa9\xe5\xba\xa6")||!strcmp(k,"oth_tmp"))
            parse_temp_range(v, &g_oth_lo, &g_oth_hi);
        else {
            int iv = atoi(v); if (iv <= 0) continue;
            if (!strcmp(k,"\xe7\x94\xb5\xe6\xb1\xa0\xe5\xbe\xaa\xe7\x8e\xaf\xe6\xac\xa1\xe6\x95\xb0"))  g_batt_cycle = iv;
            else if (!strcmp(k,"batt_cycle")) g_batt_cycle = iv;
            else if (!strcmp(k,"\xe5\xbd\x93\xe5\x89\x8d\xe7\x94\xb5\xe9\x87\x8f\xe6\x98\xbe\xe7\xa4\xba")||!strcmp(k,"fake_cap"))
                g_fake_cap = iv;
        }
    }
    fclose(f);
    if (!parsed_batt_charge_mid) {
        g_batt_charge_mid_t = g_batt_charge_start_t +
            (g_batt_charge_stop_t - g_batt_charge_start_t) / 2;
    }
    if (old_batt_charge_start != g_batt_charge_start_t ||
        old_batt_charge_mid != g_batt_charge_mid_t ||
        old_batt_charge_stop != g_batt_charge_stop_t ||
        old_batt_charge_minutes != g_batt_charge_minutes) {
        reset_batt_charge_ramp();
    }

    roll_dynamic_temps();
}

static int zone_classify(const char *ts)
{
    char lo[96]; int i;
    for (i = 0; ts[i] && i < 95; i++) {
        char c = ts[i]; lo[i] = (c>='A'&&c<='Z')?(char)(c+32):c;
    }
    lo[i] = '\0';


    if (strstr(lo,"modem")||strstr(lo,"mdm")  ||strstr(lo,"q6")     ||
        strstr(lo,"wlan") ||strstr(lo,"wcss") ||strstr(lo,"wifi")   ||
        strstr(lo,"rfa")  ||strstr(lo,"rfc")  ||strstr(lo,"rf-")    ||
        strstr(lo,"xo-")  ||strstr(lo,"ipa")  ||strstr(lo,"rpm")    ||
        strstr(lo,"sub6") ||strstr(lo,"nr5g") ||strstr(lo,"antenna")||
        strstr(lo,"qtm")  ||strstr(lo,"mmw")  ||strstr(lo,"nss")    ||
        strstr(lo,"wcn")  ||strstr(lo,"ltepa")||strstr(lo,"nrpa")) {
        if (!g_jzwz) return -1;
        return -2;
    }

    if (strstr(lo,"cpuss") ||strstr(lo,"cluster")||strstr(lo,"kryo") ||
        strstr(lo,"silver")||strstr(lo,"gold")   ||strstr(lo,"prime")||
        strstr(lo,"cpu-")  ||strstr(lo,"cpu0")   ||strstr(lo,"cpufreq")||
        (strstr(lo,"cpu")&&!strstr(lo,"cpu_c")))
        return Z_CPU;

    if (strstr(lo,"gpu")  ||strstr(lo,"kgsl")  ||strstr(lo,"adreno")||
        strstr(lo,"gpuss")||strstr(lo,"gpufreq")||strstr(lo,"mdla") ||
        strstr(lo,"apu"))
        return Z_GPU;

    if (strstr(lo,"ddr") ||strstr(lo,"dram")||strstr(lo,"llcc")||
        strstr(lo,"mem") ||strstr(lo,"npu") ||strstr(lo,"nsp") ||
        strstr(lo,"iommu"))
        return Z_MEM;


    if (strstr(lo,"shell") || strstr(lo,"skin")) return Z_SHELL;

    if (strstr(lo,"batt")   ||strstr(lo,"battery")||strstr(lo,"bms")||
        strstr(lo,"charger")||strstr(lo,"usb-therm")||
        strstr(lo,"vbat")   ||strstr(lo,"ibat"))
        return Z_BATT;

    if (strstr(lo,"pm8")   ||strstr(lo,"xo")    ||strstr(lo,"pa-therm")||
        strstr(lo,"board") ||strstr(lo,"ap_ntc")||strstr(lo,"cam")     ||
        strstr(lo,"rear")  ||strstr(lo,"oplus_thermal")||strstr(lo,"ntc")||
        strstr(lo,"ambient"))
        return Z_PERIPH;

    return Z_MISC;
}

static void discover_zones(void)
{
    g_nzones = 0;
    DIR *d = opendir("/sys/class/thermal"); if (!d) return;
    struct dirent *ent;
    while ((ent=readdir(d)) && g_nzones < MAX_ZONES) {
        if (strncmp(ent->d_name,"thermal_zone",12)) continue;
        char emul[MAX_PATH], tp[MAX_PATH];
        snprintf(emul,sizeof(emul),"/sys/class/thermal/%s/emul_temp",ent->d_name);
        snprintf(tp,  sizeof(tp),  "/sys/class/thermal/%s/type",     ent->d_name);
        if (access(emul,W_OK)) continue;
        int fd=open(tp,O_RDONLY|O_CLOEXEC); if(fd<0) continue;
        char tb[64]={0}; int n=(int)read(fd,tb,63); close(fd);
        if (n<=0) continue;
        tb[strcspn(tb,"\r\n")]='\0';
        int t=zone_classify(tb);
        if(t==-1) continue;
        int is_rf = (t==-2);
        if(is_rf) t=Z_MISC;
        strncpy(g_zones[g_nzones].path,emul,MAX_PATH-1);
        snprintf(g_zones[g_nzones].mode,MAX_PATH,
                 "/sys/class/thermal/%s/mode",ent->d_name);
        for(int j=0;j<MAX_TRIPS;j++) g_zones[g_nzones].trip_orig[j]=-1;
        g_zones[g_nzones].type=t;
        g_zones[g_nzones].rf=is_rf;
        g_zones[g_nzones].last_emul=-1;
        g_nzones++;
    }
    closedir(d);
}

static void disable_zone_governors(void)
{
    for (int i = 0; i < g_nzones; i++) {
        if (g_zones[i].type != Z_CPU &&
            g_zones[i].type != Z_GPU &&
            g_zones[i].type != Z_MEM) continue;
        if (!g_zones[i].mode[0]) continue;
        int zone_enabled =
            (g_zones[i].type == Z_CPU && g_en_cpu) ||
            (g_zones[i].type == Z_GPU && g_en_gpu) ||
            (g_zones[i].type == Z_MEM && g_en_mem);
        int change_trip_wall = (g_zones[i].type == Z_CPU) || g_en_gpu_mem_trip_wall;
        char zroot[MAX_PATH];
        strncpy(zroot, g_zones[i].mode, MAX_PATH-1);
        char *sl = strrchr(zroot, '/'); if (sl) *sl = '\0';
        for (int j = 0; j < MAX_TRIPS; j++) {
            char tp[MAX_PATH];
            snprintf(tp, sizeof(tp), "%s/trip_point_%d_temp", zroot, j);
            if (access(tp, W_OK)) break;
            int orig = rd_int(tp);
            if (g_zones[i].trip_orig[j] == -1)
                g_zones[i].trip_orig[j] = (orig > 0) ? orig : -2;
            if (zone_enabled && change_trip_wall && orig != 120000) wr_int(tp, 120000);
            else if ((!zone_enabled || !change_trip_wall) &&
                     g_zones[i].trip_orig[j] > 0 &&
                     orig != g_zones[i].trip_orig[j])
                wr_int(tp, g_zones[i].trip_orig[j]);
        }
        if (!access(g_zones[i].mode, W_OK))
            wr_str(g_zones[i].mode, zone_enabled ? "disabled\n" : "enabled\n");
    }
}

static void enable_zone_governors(void)
{
    for (int i = 0; i < g_nzones; i++) {
        if (g_zones[i].type != Z_CPU &&
            g_zones[i].type != Z_GPU &&
            g_zones[i].type != Z_MEM) continue;
        if (!g_zones[i].mode[0]) continue;
        char zroot[MAX_PATH];
        strncpy(zroot, g_zones[i].mode, MAX_PATH-1);
        char *sl = strrchr(zroot, '/'); if (sl) *sl = '\0';
        for (int j = 0; j < MAX_TRIPS; j++) {
            if (g_zones[i].trip_orig[j] == -1) break;
            char tp[MAX_PATH];
            snprintf(tp, sizeof(tp), "%s/trip_point_%d_temp", zroot, j);
            if (access(tp, W_OK)) break;
            if (g_zones[i].trip_orig[j] > 0)
                wr_int(tp, g_zones[i].trip_orig[j]);
        }
        if (!access(g_zones[i].mode, W_OK))
            wr_str(g_zones[i].mode, "enabled\n");
    }
}

static void apply_emul(void)
{


    int skip_shell_write = 0;
    if(!g_en_batt){
        if(g_shell_val != 0){
            write_shell_temp(0);
            g_shell_val = 0;
        }
        skip_shell_write = 1;
    } else if (check_cdz_file()) {
        int t = rd_temp_deci(DATA_DIR "/\xe7\x94\xb5\xe6\xb1\xa0\xe6\xb8\xa9\xe5\xba\xa6\xe5\xa2\x99.txt");
        if (t > 0)      g_shell_val = temp_to_millic(t);
        else if (t == 0) skip_shell_write = 1;

    } else {
        g_shell_val = temp_to_millic(g_batt_t);
    }
    if (!skip_shell_write) write_shell_temp(g_shell_val);
    for (int i=0;i<g_nzones;i++) {
        if (g_zones[i].rf) {
            if (g_jzwz && g_en_oth && g_rf_emul_delay == 0) {
                int val = temp_to_millic(g_oth_t);
                if (g_zones[i].last_emul != val) {
                    wr_int(g_zones[i].path, val);
                    g_zones[i].last_emul = val;
                }
            } else if((!g_jzwz || !g_en_oth) && g_zones[i].last_emul != 0) {
                wr_int(g_zones[i].path, 0);
                g_zones[i].last_emul = 0;
            }
            continue;
        }
        int val;
        switch (g_zones[i].type) {
            case Z_CPU:    if(!g_en_cpu) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_cpu_t); break;
            case Z_GPU:    if(!g_en_gpu) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_gpu_t); break;
            case Z_MEM:    if(!g_en_mem) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_mem_t); break;
            case Z_BATT:   if(!g_en_batt){ if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_batt_t); break;
            case Z_PERIPH: if(!g_en_oth) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_oth_t); break;
            case Z_MISC:   if(!g_en_oth) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_oth_t); break;
            case Z_SHELL:  if(!g_en_batt){ if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=g_shell_val; break;
            default: continue;
        }
        if (g_zones[i].last_emul != val) {
            wr_int(g_zones[i].path, val);
            g_zones[i].last_emul = val;
        }
    }
}

static void apply_emul_fast(void)
{
    int skip_shell_write = 0;
    if(!g_en_batt){
        if(g_shell_val != 0){
            write_shell_temp(0);
            g_shell_val = 0;
        }
        skip_shell_write = 1;
    } else if (check_cdz_file()) {
        int t = rd_temp_deci(DATA_DIR "/\xe7\x94\xb5\xe6\xb1\xa0\xe6\xb8\xa9\xe5\xba\xa6\xe5\xa2\x99.txt");
        if (t > 0)       g_shell_val = temp_to_millic(t);
        else if (t == 0) skip_shell_write = 1;
    } else {
        g_shell_val = temp_to_millic(g_batt_t);
    }
    if (!skip_shell_write) write_shell_temp(g_shell_val);
    for (int i=0;i<g_nzones;i++) {
        if (g_zones[i].rf) continue;
        int val;
        switch (g_zones[i].type) {
            case Z_CPU:  if(!g_en_cpu) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_cpu_t); break;
            case Z_GPU:  if(!g_en_gpu) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_gpu_t); break;
            case Z_MEM:  if(!g_en_mem) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_mem_t); break;
            case Z_BATT: if(!g_en_batt){ if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_batt_t); break;
            case Z_PERIPH: if(!g_en_oth) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_oth_t); break;
            case Z_MISC: if(!g_en_oth) { if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=temp_to_millic(g_oth_t); break;
            case Z_SHELL: if(!g_en_batt){ if(g_zones[i].last_emul!=0){wr_int(g_zones[i].path,0);g_zones[i].last_emul=0;} continue;} val=g_shell_val; break;
            default: continue;
        }
        if (g_zones[i].last_emul != val) {
            wr_int(g_zones[i].path, val);
            g_zones[i].last_emul = val;
        }
    }
}

static void clear_emul(void)
{
    for (int i=0;i<g_nzones;i++) { wr_int(g_zones[i].path,0); g_zones[i].last_emul=-1; }
    enable_zone_governors();
}

static void clear_cooling_devs(void)
{
    DIR *d=opendir("/sys/class/thermal"); if(!d) return;
    struct dirent *ent;
    while ((ent=readdir(d))) {
        if (strncmp(ent->d_name,"cooling_device",14)) continue;
        char path[MAX_PATH];
        snprintf(path,sizeof(path),"/sys/class/thermal/%s/cur_state",ent->d_name);
        if (!access(path,W_OK)) {
            int v = rd_int(path);
            if (v != 0) wr_int(path,0);
        }
    }
    closedir(d);
}

static void screen_wake_boost(void)
{
    pid_t p = fork();
    if (p < 0) {
        disable_zone_governors();
        clear_cooling_devs();
        apply_emul_fast();
        return;
    }
    if (p > 0) {
        int st; waitpid(p, &st, 0);
        return;
    }
    setsid();
    pid_t g2 = fork();
    if (g2 < 0) _exit(1);
    if (g2 > 0) _exit(0);
    disable_zone_governors();
    clear_cooling_devs();
    apply_emul_fast();
    _exit(0);
}

static void write_fake_batt_file(void)
{
    char buf[24]; int n=snprintf(buf,sizeof(buf),"%d\n",temp_to_deci(g_batt_t));
    if(n>0) write_file_if_changed(g_fakepath, buf, (size_t)n, 0644);
}

static int batt_bind_alive(void)
{
    if(!g_batt_bound) return 0;
    int expected = temp_to_deci(g_batt_t);
    int v = rd_int(BATT_SYS"/temp");
    if(v >= 0 && v >= expected - 5000 && v <= expected + 5000) return 1;
    return 0;
}

static void bind_batt_temp(void)
{
    const char *dst=BATT_SYS"/temp";
    if(access(dst,F_OK)) return;
    if(g_batt_bound && batt_bind_alive()) return;
    if(access(g_fakepath,F_OK)) write_fake_batt_file();
    unmount_all(dst);
    if(mount(g_fakepath,dst,NULL,MS_BIND,NULL)==0){
        g_batt_bound=1;
    } else {
        g_batt_bound=0;
    }
}

static void bind_oplus_temp(void)
{
    int cnt=0;
    if(access(g_fakepath,F_OK)) write_fake_batt_file();
    for(int i=0;OPLUS_TEMP_NODES[i];i++){
        if(access(OPLUS_TEMP_NODES[i],F_OK)) continue;
        unmount_all(OPLUS_TEMP_NODES[i]);
        if(mount(g_fakepath,OPLUS_TEMP_NODES[i],NULL,MS_BIND,NULL)==0) cnt++;
    }
    if(cnt) g_batt_bound=1;
}

static void unbind_bcc_parms(void);
static void unbind_cycle_count(void);

static void unbind_batt_temp(void)
{
    unmount_all(BATT_SYS"/temp");
    for(int i=0;OPLUS_TEMP_NODES[i];i++) unmount_all(OPLUS_TEMP_NODES[i]);
    g_batt_bound=0;
    if(!g_en_cycle){
        unmount_all(BATT_SYS"/uevent");
        g_uev_bound=0;
        if(g_real_uev_fd>=0){close(g_real_uev_fd);g_real_uev_fd=-1;}
    }
    unbind_bcc_parms();
}

#define BCC_PARMS_PATH "/sys/class/oplus_chg/battery/bcc_parms"
#define BCC_TEMP_IDX   7

static void write_fake_bcc_file(void)
{
    if(!g_en_batt || g_real_bcc_fd < 0 || !g_fakebccpath[0]) return;

    int cur_temp = temp_to_deci(g_batt_t);

    char real_buf[512] = {0};
    int real_n = 0;
    lseek(g_real_bcc_fd, 0, SEEK_SET);
    real_n = (int)read(g_real_bcc_fd, real_buf, sizeof(real_buf) - 1);
    if(real_n < 0) real_n = 0;
    real_buf[real_n] = '\0';

    int l = real_n;
    while(l > 0 && (real_buf[l-1] == '\n' || real_buf[l-1] == '\r')) real_buf[--l] = '\0';
    real_n = l;

    char out[512];
    int out_len = 0;

    if(real_n > 0){
        char tmp[512]; strncpy(tmp, real_buf, sizeof(tmp) - 1);
        char *p = tmp;
        int idx = 0, first = 1;
        while(p && *p){
            char *comma = strchr(p, ',');
            if(comma) *comma = '\0';

            if(!first) out_len += snprintf(out + out_len, sizeof(out) - out_len, ",");

            if(idx == 7)
                out_len += snprintf(out + out_len, sizeof(out) - out_len, "%d", cur_temp);
            else
                out_len += snprintf(out + out_len, sizeof(out) - out_len, "%s", p);

            first = 0; idx++;
            p = comma ? comma + 1 : NULL;
        }
    } else {

        out_len = snprintf(out, sizeof(out), "0,0,0,0,0,0,0,%d,0,0,0,0,0,0,0,0,0,0,0", cur_temp);
    }
    out[out_len] = '\0';

    char file_out[sizeof(out) + 2];
    int file_len = snprintf(file_out, sizeof(file_out), "%s\n", out);
    if(file_len > 0)
        write_file_if_changed(g_fakebccpath, file_out, (size_t)file_len, 0644);
}

static void bind_bcc_parms(void)
{
    unmount_all(BCC_PARMS_PATH);
    if(access(BCC_PARMS_PATH,F_OK)) return;
    g_real_bcc_fd=open(BCC_PARMS_PATH,O_RDONLY|O_CLOEXEC);
    if(g_real_bcc_fd<0){ return; }
    int fd=open(g_fakebccpath,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);
    if(fd>=0) close(fd);
    write_fake_bcc_file();
    if(mount(g_fakebccpath,BCC_PARMS_PATH,NULL,MS_BIND,NULL)==0){
        g_bcc_bound=1;
    } else {
        close(g_real_bcc_fd); g_real_bcc_fd=-1;
    }
}

static void unbind_bcc_parms(void)
{
    unmount_all(BCC_PARMS_PATH);
    g_bcc_bound=0;
    if(g_real_bcc_fd>=0){close(g_real_bcc_fd);g_real_bcc_fd=-1;}
}

#define CYCLE_COUNT_PATH BATT_SYS"/cycle_count"

static const char *CYCLE_NODES[] = {
    BATT_SYS"/cycle_count",
    BATT_SYS"/battery_cycle",
    BATT_SYS"/batt_cycle",
    BATT_SYS"/cycle",
    "/sys/class/power_supply/battery/cycle_count",
    "/sys/class/power_supply/bms/cycle_count",
    "/sys/class/power_supply/bms/battery_cycle",
    "/sys/class/oplus_chg/battery/cycle_count",
    "/sys/class/oplus_chg/battery/charge_cycle",
    "/sys/class/oplus_chg/battery/batt_chargecycles",
    "/sys/class/oplus_chg/battery/battery_cc",
    "/sys/devices/platform/soc/soc:oplus,mms_gauge/oplus_mms/gauge/battery/cycle_count",
    "/sys/class/qcom-battery/fake_cycle",
    "/sys/class/xm_power/fg_master/qmax_cyclecount",
    "/sys/class/xm_power/fg_master/cyclecount",
    "/sys/devices/virtual/xm_power/fg_master/qmax_cyclecount",
    "/sys/devices/virtual/xm_power/fg_master/cyclecount",
    NULL
};

static void write_fake_cycle_file(void)
{
    if(!g_en_cycle || !g_fakecyclepath[0]) return;
    char buf[24]; int n=snprintf(buf,sizeof(buf),"%d\n",g_batt_cycle);
    if(n>0) write_file_if_changed(g_fakecyclepath, buf, (size_t)n, 0644);
}

static void bind_cycle_count(void)
{
    if(!g_en_cycle) return;
    write_fake_cycle_file();
    int cnt=0;
    for(int i=0;CYCLE_NODES[i];i++){
        if(access(CYCLE_NODES[i],F_OK)) continue;
        unmount_all(CYCLE_NODES[i]);
        if(mount(g_fakecyclepath,CYCLE_NODES[i],NULL,MS_BIND,NULL)==0) cnt++;
    }
    g_cycle_bound = cnt ? 1 : 0;
}

static void unbind_cycle_count(void)
{
    for(int i=0;CYCLE_NODES[i];i++) unmount_all(CYCLE_NODES[i]);
    g_cycle_bound=0;
}

#define CAP_PATH      BATT_SYS"/capacity"
#define CHIP_SOC_PATH "/sys/class/oplus_chg/battery/chip_soc"

static void unbind_cap(void)
{
    umount2(CAP_PATH,MNT_DETACH);
    g_cap_bound=0;
    if(g_real_cap_fd>=0){close(g_real_cap_fd);g_real_cap_fd=-1;}
    umount2(CHIP_SOC_PATH,MNT_DETACH);
    g_chip_soc_bound=0;
}

static int batt_level_valid(int v)
{
    return v >= 0 && v <= 100;
}

static int read_oplus_charge_type(void)
{
    if(rd_int("/sys/class/power_supply/usb/online") != 1) return 0;

    int fd = open("/sys/class/oplus_chg/battery/battery_log_content", O_RDONLY|O_CLOEXEC);
    if(fd < 0) return 0;
    char buf[2048];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if(n <= 0) return 0;
    buf[n] = '\0';

    char *p = buf;
    for(int i = 0; i < 9; i++){
        p = strchr(p, ',');
        if(!p) return 0;
        p++;
    }
    while((unsigned char)*p <= ' ' && *p) p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if(end == p || v < 0 || v > 100) return 0;
    return (int)v;
}

static int charge_type_keeps_vendor_plug(int charge_type)
{
    return charge_type >= 6 && charge_type <= 15;
}

static int read_dumpsys_plug_state(int *ac, int *usb, int *charge_type)
{
    int type = read_oplus_charge_type();
    int cur_ac  = rd_int("/sys/class/power_supply/ac/online");
    int cur_usb = rd_int("/sys/class/power_supply/usb/online");
    if(cur_ac  < 0) cur_ac  = 0;
    if(cur_usb < 0) cur_usb = 0;

    if(charge_type) *charge_type = type;
    if(charge_type_keeps_vendor_plug(type)){
        *ac = 0;
        *usb = 0;
        return 0;
    }

    if(cur_usb > 0){
        cur_ac = 1;
        cur_usb = 0;
    } else if(cur_ac <= 0 && charger_online())
        cur_ac = 1;
    *ac = cur_ac > 0 ? 1 : 0;
    *usb = cur_usb > 0 ? 1 : 0;
    return 1;
}

static void run_dumpsys_batt_update(int reset, int cur_temp, int status_int, int level)
{
    int cur_ac, cur_usb, charge_type;
    int write_plug = read_dumpsys_plug_state(&cur_ac, &cur_usb, &charge_type);
    const char *prefix = reset ? "dumpsys battery reset &&" : "";
    char cmd[320];

    if(cur_temp < 0){
        if(batt_level_valid(level)){
            if(write_plug)
                snprintf(cmd,sizeof(cmd),
                    "%sdumpsys battery set status %d &&"
                    "dumpsys battery set level %d &&"
                    "dumpsys battery set ac %d &&"
                    "dumpsys battery set usb %d &&"
                    "dumpsys battery set present 1",
                    prefix, status_int, level, cur_ac, cur_usb);
            else
                snprintf(cmd,sizeof(cmd),
                    "%sdumpsys battery set status %d &&"
                    "dumpsys battery set level %d &&"
                    "dumpsys battery set present 1",
                    prefix, status_int, level);
        } else {
            if(write_plug)
                snprintf(cmd,sizeof(cmd),
                    "%sdumpsys battery set status %d &&"
                    "dumpsys battery set ac %d &&"
                    "dumpsys battery set usb %d &&"
                    "dumpsys battery set present 1",
                    prefix, status_int, cur_ac, cur_usb);
            else
                snprintf(cmd,sizeof(cmd),
                    "%sdumpsys battery set status %d &&"
                    "dumpsys battery set present 1",
                    prefix, status_int);
        }
        run_sync(cmd);
        return;
    }

    if(batt_level_valid(level)){
        if(write_plug)
            snprintf(cmd,sizeof(cmd),
                "%sdumpsys battery set temp %d &&"
                "dumpsys battery set status %d &&"
                "dumpsys battery set level %d &&"
                "dumpsys battery set ac %d &&"
                "dumpsys battery set usb %d &&"
                "dumpsys battery set present 1",
                prefix, cur_temp, status_int, level, cur_ac, cur_usb);
        else
            snprintf(cmd,sizeof(cmd),
                "%sdumpsys battery set temp %d &&"
                "dumpsys battery set status %d &&"
                "dumpsys battery set level %d &&"
                "dumpsys battery set present 1",
                prefix, cur_temp, status_int, level);
    } else {
        if(write_plug)
            snprintf(cmd,sizeof(cmd),
                "%sdumpsys battery set temp %d &&"
                "dumpsys battery set status %d &&"
                "dumpsys battery set ac %d &&"
                "dumpsys battery set usb %d &&"
                "dumpsys battery set present 1",
                prefix, cur_temp, status_int, cur_ac, cur_usb);
        else
            snprintf(cmd,sizeof(cmd),
                "%sdumpsys battery set temp %d &&"
                "dumpsys battery set status %d &&"
                "dumpsys battery set present 1",
                prefix, cur_temp, status_int);
    }
    run_sync(cmd);
}

static int batt_temp_for_dumpsys(void)
{
    if(g_en_batt) return temp_to_deci(g_batt_t);
    return rd_int(BATT_SYS"/temp");
}

static int parse_batt_level(const char *buf, int *out)
{
    const char *p = buf;
    while ((unsigned char)*p <= ' ' && *p) p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    while ((unsigned char)*end <= ' ' && *end) end++;
    if (*end != '\0' || v < 0 || v > 100) return 0;
    *out = (int)v;
    return 1;
}

static int read_batt_level_fd(int fd)
{
    if (fd < 0) return -1;
    char b[16] = {0};
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    int n = (int)read(fd, b, sizeof(b) - 1);
    if (n <= 0) return -1;
    b[n] = '\0';
    int v = -1;
    return parse_batt_level(b, &v) ? v : -1;
}

static int read_batt_level_path(const char *path)
{
    int fd = open(path, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return -1;
    int v = read_batt_level_fd(fd);
    close(fd);
    return v;
}

static int get_cap_spoof_val(int real_cap_override)
{
    int real_cap = real_cap_override;
    if(real_cap < 0 && g_real_cap_fd >= 0)
        real_cap = read_batt_level_fd(g_real_cap_fd);

    if(g_en_cap) return g_fake_cap;
    if(g_en_supersave){
        if(check_cdz_file()) return -1;
        if(real_cap >= 0 && real_cap <= 3) return 3;
        if(real_cap < 0 && g_real_cap_fd >= 0) return 3;
    }
    return -1;
}

static int batt_level_for_dumpsys(void)
{
    int v = get_cap_spoof_val(-1);
    if (batt_level_valid(v)) return v;
    if (g_en_supersave && check_cdz_file()) {
        v = read_batt_level_fd(g_real_cap_fd);
        if (batt_level_valid(v)) return v;
    }

    v = read_batt_level_path(CAP_PATH);
    if (batt_level_valid(v)) return v;

    v = read_batt_level_fd(g_real_cap_fd);
    return batt_level_valid(v) ? v : -1;
}

static void refresh_cap_spoof(void)
{
    int real_cap = -1;
    if(!g_cap_bound) real_cap = read_batt_level_path(CAP_PATH);
    int val = get_cap_spoof_val(real_cap);

    if(val < 0){
        if(g_cap_bound){
            int real_lv = read_batt_level_fd(g_real_cap_fd);
            unbind_cap();
            if(batt_level_valid(real_lv)){
                char cmd[64];
                snprintf(cmd,sizeof(cmd),"dumpsys battery set level %d",real_lv);
                run_sync(cmd);
            }
        }
        return;
    }

    if(!g_cap_bound){
        if(access(CAP_PATH,F_OK)){ return; }
        g_real_cap_fd = open(CAP_PATH, O_RDONLY|O_CLOEXEC);
        if(g_real_cap_fd < 0){ return; }
        val = get_cap_spoof_val(-1);
        if(val < 0) val = g_fake_cap;
        { char b[16];int n=snprintf(b,sizeof(b),"%d\n",val);
          if(n>0) write_file_if_changed(g_fakecappath,b,(size_t)n,0644); }
        if(mount(g_fakecappath,CAP_PATH,NULL,MS_BIND,NULL)==0){
            g_cap_bound=1;
            { char cmd[64]; snprintf(cmd,sizeof(cmd),"dumpsys battery set level %d",val); run_sync(cmd); }
        } else {
            close(g_real_cap_fd); g_real_cap_fd=-1;
        }

        if(g_cap_bound && !g_chip_soc_bound && !access(CHIP_SOC_PATH,F_OK)){
            if(mount(g_fakecappath,CHIP_SOC_PATH,NULL,MS_BIND,NULL)==0)
                g_chip_soc_bound=1;
        }
        return;
    }

    static int last_cap_val = -1;
    if(val != last_cap_val){
        { char b[16];int n=snprintf(b,sizeof(b),"%d\n",val);
          if(n>0) write_file_if_changed(g_fakecappath,b,(size_t)n,0644); }
        last_cap_val=val;

        { char cmd[64]; snprintf(cmd,sizeof(cmd),"dumpsys battery set level %d",val); run_sync(cmd); }
    }

    if(!g_chip_soc_bound && !access(CHIP_SOC_PATH,F_OK)){
        if(mount(g_fakecappath,CHIP_SOC_PATH,NULL,MS_BIND,NULL)==0){
            g_chip_soc_bound=1;
        }
    }
}

static void refresh_fake_uevent(void)
{
    if(g_real_uev_fd<0 || !g_fakeuevpath[0]) return;

    int cur_temp  = g_en_batt ? temp_to_deci(g_batt_t) : -1;
    int cur_cycle = g_batt_cycle;



    if(lseek(g_real_uev_fd,0,SEEK_SET)<0) return;
    char buf[8192]={0};
    int n=(int)read(g_real_uev_fd,buf,sizeof(buf)-1);
    if(n<=0) return;
    buf[n]='\0';

    char out[16384];
    size_t out_len = 0;
    char *p=buf;
    while(*p){
        char *nl=strchr(p,'\n');
        int len=nl?(int)(nl-p+1):(int)strlen(p);
        if(g_en_batt && strncmp(p,"POWER_SUPPLY_TEMP=",18)==0){
            char tmp[64];
            int m=snprintf(tmp,sizeof(tmp),"POWER_SUPPLY_TEMP=%d\n",cur_temp);
            if(m<0 || (size_t)m >= sizeof(tmp) || (size_t)m > sizeof(out)-out_len) return;
            memcpy(out+out_len,tmp,(size_t)m); out_len += (size_t)m;
        }
        else if(g_en_cycle && strncmp(p,"POWER_SUPPLY_CYCLE_COUNT=",25)==0){
            char tmp[64];
            int m=snprintf(tmp,sizeof(tmp),"POWER_SUPPLY_CYCLE_COUNT=%d\n",cur_cycle);
            if(m<0 || (size_t)m >= sizeof(tmp) || (size_t)m > sizeof(out)-out_len) return;
            memcpy(out+out_len,tmp,(size_t)m); out_len += (size_t)m;
        }
        else{
            if((size_t)len > sizeof(out)-out_len) return;
            memcpy(out+out_len,p,(size_t)len); out_len += (size_t)len;
        }
        p+=len; if(!nl) break;
    }
    write_file_if_changed(g_fakeuevpath,out,out_len,0644);
}

static void bind_batt_uevent(void)
{
    const char *dst=BATT_SYS"/uevent";
    unmount_all(dst);
    if(access(dst,F_OK)){ return;}
    g_real_uev_fd=open(dst,O_RDONLY|O_CLOEXEC);
    if(g_real_uev_fd<0){ return;}
    if(access(g_fakeuevpath,F_OK)){
        int fd=open(g_fakeuevpath,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);
        if(fd>=0) close(fd);
    }
    refresh_fake_uevent();
    if(mount(g_fakeuevpath,dst,NULL,MS_BIND,NULL)==0){
        g_uev_bound=1;
    } else {
        close(g_real_uev_fd); g_real_uev_fd=-1;
    }
}

static void force_batt_reread(void)
{
    if(!g_en_batt) return;


    int status_int=3;
    { char sb[32]={0};
      int fd=open(BATT_SYS"/status",O_RDONLY|O_CLOEXEC);
      if(fd>=0){int n=(int)read(fd,sb,31);close(fd);
        if(n>0){sb[strcspn(sb,"\r\n")]='\0';
          if     (strncasecmp(sb,"Charging",    8)==0) status_int=2;
          else if(strncasecmp(sb,"Full",        4)==0) status_int=5;
          else if(strncasecmp(sb,"Not charging",12)==0) status_int=4;
        }
      }
    }
    int level = batt_level_for_dumpsys();
    run_dumpsys_batt_update(1, batt_temp_for_dumpsys(), status_int, level);
}
static void mount_batt(void)
{
    if(g_en_batt){
        write_fake_batt_file(); bind_batt_temp();
        bind_oplus_temp(); bind_batt_uevent(); bind_bcc_parms();
    } else if(g_en_cycle) {
        bind_batt_uevent();
    }

    if(g_en_cycle) bind_cycle_count();
    refresh_cap_spoof();


    if(g_en_batt) force_batt_reread();
}

static void update_fake_batt(void)
{
    if(g_en_batt){

        if(g_batt_bound && !batt_bind_alive()){
            g_batt_bound  = 0;
            g_uev_bound   = 0;
            g_bcc_bound   = 0;
            g_cycle_bound = 0;
            if(g_real_uev_fd >= 0){ close(g_real_uev_fd); g_real_uev_fd = -1; }
            if(g_real_bcc_fd >= 0){ close(g_real_bcc_fd); g_real_bcc_fd = -1; }
        }
        if(!g_batt_bound){
            write_fake_batt_file(); bind_batt_temp();
            bind_oplus_temp(); bind_batt_uevent(); bind_bcc_parms();
            if(g_en_cycle) bind_cycle_count();
        } else {
            write_fake_batt_file();

            if(g_en_cycle){
                if(!g_cycle_bound){ bind_cycle_count(); force_batt_reread(); }
                else               write_fake_cycle_file();
            } else {
                unbind_cycle_count();
            }
            refresh_fake_uevent(); write_fake_bcc_file();
        }
        refresh_cap_spoof();
    } else {

        unbind_batt_temp();
        if(g_en_cycle){
            if(!g_uev_bound) bind_batt_uevent();
            else             refresh_fake_uevent();
        }

        if(g_en_cycle){
            if(!g_cycle_bound){ bind_cycle_count(); }
            else               write_fake_cycle_file();
        } else {
            unbind_cycle_count();
        }
    }
}

#define DUMPSYS_KEEPALIVE_TICKS 300

static void dumpsys_batt_temp(void)
{
    static int last_status=-1,last_level=-1,last_temp=-1,last_ac=-1,last_usb=-1,last_charge_type=-1;
    static int keepalive=0, batt_was_en=-1;

    if(!g_en_batt){
        if(batt_was_en!=0){
            run_async("dumpsys battery reset");
            last_status=last_level=last_temp=last_ac=last_usb=last_charge_type=-1;
            keepalive=0; batt_was_en=0;
        }
    } else {
        batt_was_en=1;
    }

    if(g_vooc_handshake_guard > 0 &&
       !charge_type_keeps_vendor_plug(read_oplus_charge_type())){ return; }

    int status_int=3;
    { char sb[32]={0};
      int fd=open(BATT_SYS"/status",O_RDONLY|O_CLOEXEC);
      if(fd>=0){int n=(int)read(fd,sb,31);close(fd);
        if(n>0){sb[strcspn(sb,"\r\n")]='\0';
          if     (strncasecmp(sb,"Charging",    8)==0) status_int=2;
          else if(strncasecmp(sb,"Full",        4)==0) status_int=5;
          else if(strncasecmp(sb,"Not charging",12)==0) status_int=4;
        }
      }
    }

    int level = batt_level_for_dumpsys();
    if(!batt_level_valid(level)){
        if(batt_level_valid(last_level)) level = last_level;
        else return;
    }

    int cur_temp = batt_temp_for_dumpsys();
    int cur_ac, cur_usb, cur_charge_type;
    int write_plug = read_dumpsys_plug_state(&cur_ac, &cur_usb, &cur_charge_type);



    if(g_skip_dumpsys_set > 0){
        if(charge_type_keeps_vendor_plug(cur_charge_type)){
            g_skip_dumpsys_set = 0;
            run_dumpsys_batt_update(1, cur_temp, status_int, level);
            last_status=status_int; last_level=level;
            last_temp=cur_temp; last_ac=cur_ac; last_usb=cur_usb; last_charge_type=cur_charge_type;
            keepalive = g_cap_bound ? 60 : DUMPSYS_KEEPALIVE_TICKS;
            return;
        }
        g_skip_dumpsys_set--;
        last_status=status_int; last_level=level;
        last_temp=cur_temp; last_ac=cur_ac; last_usb=cur_usb; last_charge_type=cur_charge_type;
        keepalive = g_cap_bound ? 60 : DUMPSYS_KEEPALIVE_TICKS;


        if(g_skip_dumpsys_set == 0) last_status = -1;
        return;
    }

    int changed=(status_int!=last_status||level!=last_level||
                 cur_temp!=last_temp||cur_ac!=last_ac||cur_usb!=last_usb||
                 cur_charge_type!=last_charge_type);
    if(!changed&&keepalive>0){keepalive--;return;}

    run_dumpsys_batt_update(write_plug ? 0 : 1, cur_temp, status_int, level);



    keepalive = g_cap_bound ? 60 : DUMPSYS_KEEPALIVE_TICKS;

    last_status=status_int; last_level=level;
    last_temp=cur_temp; last_ac=cur_ac; last_usb=cur_usb; last_charge_type=cur_charge_type;
}

static void charging_boost(void)
{
    if(!charge_nodes_enabled()) return;

    if(!access("/proc/oplus-votable/GAUGE_UPDATE/force_val",W_OK)){
        chmod("/proc/oplus-votable/GAUGE_UPDATE/force_val",   0666);
        chmod("/proc/oplus-votable/GAUGE_UPDATE/force_active",0666);
        wr_int("/proc/oplus-votable/GAUGE_UPDATE/force_val",   1000);
        wr_int("/proc/oplus-votable/GAUGE_UPDATE/force_active",   1);
    }

    {
        const char *va0[]={
            "/proc/oplus-votable/CHG_DISABLE/force_active",
            "/proc/oplus-votable/CHG_SUSPEND/force_active",
            "/proc/oplus-votable/WIRED_CHARGE_DONE/force_active",
            "/proc/oplus-votable/COOL_DOWN/force_active",
            "/proc/oplus-votable/WIRED_CURR_CTRL/force_active",

            "/proc/oplus-votable/LOW_SOC/force_active",
            "/proc/oplus-votable/TRICKLE_CHG/force_active",
            "/proc/oplus-votable/LOW_CAPACITY/force_active",
            "/proc/oplus-votable/PRE_CHG/force_active",
            "/proc/oplus-votable/LOW_POWER/force_active",
            NULL};
        for(int i=0;va0[i];i++)
            if(!access(va0[i],W_OK)) wr_int(va0[i],0);
    }

    if (!access("/sys/class/power_supply/battery/siop_level", W_OK)) {
        wr_int("/sys/class/power_supply/battery/siop_level", 100);
    }

    const char *chg0[]={

        "/sys/class/oplus_chg/battery/thermal_enable",
        "/sys/class/oplus_chg/battery/cool_down",
        "/sys/class/oplus_chg/battery/chg_cool_down",
        "/sys/class/oplus_chg/battery/slow_chg_enable",
        "/sys/class/oplus_chg/battery/chg_cycle_enable",
        "/sys/class/oplus_chg/battery/stop_chg",
        "/sys/class/oplus_chg/battery/aging_fcc_voted",
        "/sys/class/oplus_chg/battery/chg_protect_enable",
        "/sys/class/oplus_chg/battery/battery_notify_code",
        "/sys/class/oplus_chg/battery/input_suspend",
        "/sys/class/oplus_chg/battery/thermal_ctrl",
        "/sys/class/oplus_chg/battery/super_endurance_mode",
        "/sys/class/oplus_chg/battery/screen_off_chg_enable",
        "/sys/class/oplus_chg/battery/screen_off_slow_chg",
        "/sys/class/oplus_chg/battery/night_chg_enable",

        "/sys/class/oplus_chg/common/chg_disable_votable",
        "/sys/class/oplus_chg/common/cool_down",
        "/sys/class/oplus_chg/common/slow_chg_enable",
        "/sys/class/oplus_chg/common/chg_cycle_status",
        "/sys/class/oplus_chg/common/pps_disable_votable",
        "/sys/class/oplus_chg/common/ufcs_disable_votable",
        "/sys/class/oplus_chg/common/pd_disable_votable",
        "/sys/class/oplus_chg/common/screen_off_chg",
        "/sys/class/oplus_chg/common/night_chg_enable",

        "/sys/kernel/oplus_chg/battery/thermal_ctrl",

        "/sys/class/power_supply/battery/input_suspend",
        "/sys/class/power_supply/battery/thermal_feature_on",
        "/sys/class/power_supply/battery/charge_control_limit",
        "/sys/class/power_supply/battery/screen_off_chg",
        "/sys/class/power_supply/battery/restricted_charging",
        "/sys/class/power_supply/battery/batt_slate_mode",
        "/sys/class/power_supply/battery/store_mode",
        "/sys/class/power_supply/battery/batt_misc_event",
        "/sys/class/power_supply/battery/mmi_chrg_dis",
        "/sys/class/power_supply/battery/screen_state",
        "/sys/class/power_supply/battery/smart_charging_activation_enabled",
        "/sys/class/power_supply/battery/charge_rate",

        "/sys/class/oplus_chg/battery/low_soc_limit",
        "/sys/class/oplus_chg/common/low_soc_limit",
        "/sys/kernel/oplus_chg/battery/low_chg_current",

        "/sys/class/power_supply/battery/bd_trickle_cnt",
        "/sys/class/power_supply/battery/bd_trickle_eoc",

        "/sys/class/qcom-battery/restrict_chg",

        "/sys/devices/platform/charger/screen_off_throttle",
        "/sys/devices/platform/soc/soc:google,charger/charge_disable",

        "/sys/devices/platform/vivo_battery/screen_off_charge",
        "/sys/devices/platform/vivo_battery/charge_sleep_mode",

        "/sys/class/hw_power/charger/charge_data/iin_thermal_aux",

        "/sys/class/power_supply/battery/chg_control_limit_max",
        "/sys/class/power_supply/battery/lim_charge",
        "/sys/class/mi_battchg/mi_battchg/thermal_level",
        "/sys/class/mi_battchg/mi_battchg/thermal_enable",
        NULL};
    for(int i=0;chg0[i];i++) if(!access(chg0[i],W_OK)) wr_int(chg0[i],0);

    const char *chg1[]={
        "/sys/class/oplus_chg/battery/battery_charging_enabled",
        "/sys/class/oplus_chg/battery/fast_chg_allow",
        "/sys/class/oplus_chg/battery/pd_allow",
        "/sys/class/oplus_chg/battery/vooc_allow",
        "/sys/class/oplus_chg/battery/wls_boost_en",
        "/sys/class/oplus_chg/battery/wired_boost_enable",
        "/sys/class/oplus_chg/common/pd_allow",
        "/sys/class/oplus_chg/common/wired_boost_enable",
        "/sys/kernel/oplus_chg/battery/voocphy_enable",
        "/sys/class/power_supply/battery/battery_charging_enabled",
        "/sys/class/power_supply/battery/charge_enabled",
        "/sys/class/power_supply/battery/charging_enabled",
        "/sys/class/hw_power/charger/charge_data/enable_charger",
        NULL};
    for(int i=0;chg1[i];i++) if(!access(chg1[i],W_OK)) wr_str(chg1[i],"1\n");

    const char *cur_n[]={
        "/sys/class/power_supply/battery/constant_charge_current_max",
        "/sys/class/power_supply/battery/fastchg_current_max",
        "/sys/class/oplus_chg/battery/vooc_chg_current",
        "/sys/class/oplus_chg/battery/vooc_max_current",
        "/sys/kernel/debug/charger/fastcharge_current",

        "/sys/class/oplus_chg/common/wired_icl_votable",
        "/sys/class/oplus_chg/common/wired_fcc_votable",
        "/sys/class/mi_battchg/mi_battchg/chg_current",
        NULL};
    for(int i=0;cur_n[i];i++) if(!access(cur_n[i],W_OK)) wr_int(cur_n[i],65000000);

    const char *inp_n[]={
        "/sys/class/power_supply/usb/input_current_limit",
        "/sys/class/power_supply/usb/current_max",
        "/sys/class/power_supply/ac/input_current_limit",
        "/sys/class/oplus_chg/usb/hw_current_max",
        "/sys/class/mi_battchg/mi_battchg/input_current",
        NULL};
    for(int i=0;inp_n[i];i++) if(!access(inp_n[i],W_OK)) wr_int(inp_n[i],5000000);

    {
        const char *scfg = "/sys/class/thermal/thermal_message/sconfig";
        if(!access(scfg, W_OK)){
            int orig = rd_int(scfg);
            wr_int(scfg, 0);

            if(orig > 0 && !access("/sys/class/power_supply/usb/apsd_rerun", W_OK))
                wr_int("/sys/class/power_supply/usb/apsd_rerun", 1);
        }
    }

    run_async(
        "setprop persist.vendor.charge.thermal.control 0\n"
        "setprop ro.oplus.charge.thermal.limit 0\n"
        "setprop persist.vendor.oplus.charge.cooldown 0\n"
        "setprop persist.vendor.oplus.slow_chg_enable 0\n"
        "setprop persist.oplus.chg.vooc.allow 1\n"
        "setprop persist.oplus.chg.ufcs.allow 1\n"
        "setprop persist.oplus.chg.pps.allow 1\n"
        "setprop persist.oplus.chg.voocphy.allow 1\n"
        "setprop persist.oplus.chg.svooc.allow 1\n"
        "setprop persist.oplus.chg.flash_charge.allow 1\n"
        "setprop persist.oplus.chg.cool_down 0\n"
        "setprop persist.oplus.chg.screenoff.decharge 0\n"
        "setprop persist.oplus.chg.screen_off_decharge 0\n"
        "setprop persist.oplus.chg.screen_off_slow_chg 0\n"
        "setprop persist.oplus.chg.night_charging 0\n"
        "setprop vendor.battery.charge.disable 0\n"
        "setprop persist.sys.powerhal.thermal.disabled 1\n"
        "setprop vendor.battery.restrict.charging 0\n"
        "setprop persist.sys.charge.restrict 0\n"
        "setprop persist.sys.charge.screenoff 0\n"
        "setprop persist.sys.charge.sleep_mode 0\n"
        "setprop persist.chg.screen_off_reduce 0\n"
        "setprop persist.vendor.chg.screen_off_reduce 0\n"
        "setprop vendor.battery.screenoff.decharge 0\n"
        "setprop persist.vendor.battery.sleep_charging 0\n"
        "setprop persist.vendor.dgb.sleep.chg.enabled 0\n"
        "setprop persist.hw.charge.screenoff 0\n"
        "setprop persist.vivo.charge.screenoff 0\n"
        "setprop persist.vivo.charger.sleep_mode 0\n"
        "setprop persist.mmi.charge.screenoff 0\n"
        "setprop persist.samsung.charge.screenoff 0"
    );

    if(!access("/proc/game_opt/disable_cpufreq_limit",W_OK))
        wr_int("/proc/game_opt/disable_cpufreq_limit",1);

    {
        static const struct { const char *val; const char *active; } vf[] = {
            { "/proc/oplus-votable/FCC/force_val",
              "/proc/oplus-votable/FCC/force_active" },
            { "/proc/oplus-votable/ICL/force_val",
              "/proc/oplus-votable/ICL/force_active" },
            { "/proc/oplus-votable/WIRED_CURR_CTRL/force_val",
              "/proc/oplus-votable/WIRED_CURR_CTRL/force_active" },
            { NULL, NULL }
        };
        for (int i = 0; vf[i].val; i++) {
            if (access(vf[i].val,    W_OK)) continue;
            if (access(vf[i].active, W_OK)) continue;
            wr_int(vf[i].val,    65000);
            wr_int(vf[i].active, 1);
        }
    }

    {
        static const char *xm_thermal[] = {
            "/sys/class/xm_power/charger/charger_thermal/wired_thermal_remove",
            "/sys/class/xm_power/charger/charger_thermal/wireless_thermal_remove",
            "/sys/class/qcom-battery/thermal_remove",
            NULL
        };
        for (int i = 0; xm_thermal[i]; i++)
            if (!access(xm_thermal[i], W_OK)) wr_int(xm_thermal[i], 1);
    }
}

static int charger_online(void)
{
    int ac  = rd_int("/sys/class/power_supply/ac/online");
    int usb = rd_int("/sys/class/power_supply/usb/online");
    int wl  = rd_int("/sys/class/power_supply/wireless/online");
    int dc  = rd_int("/sys/class/power_supply/dc/online");
    return ac > 0 || usb > 0 || wl > 0 || dc > 0 || check_cdz_file();
}

static void maintain_charging(void)
{
    if(!charge_nodes_enabled()){
        if (g_vooc_handshake_guard > 0) g_vooc_handshake_guard--;
        return;
    }

    {
        int ac  = rd_int("/sys/class/power_supply/ac/online");
        int usb = rd_int("/sys/class/power_supply/usb/online");
        if ((ac <= 0) && (usb <= 0)) return;
    }

    {
        const char *ss = "/sys/class/power_supply/battery/screen_state";
        if (!access(ss, W_OK))
            if (rd_int(ss) != 1) wr_int(ss, 1);
    }

    static const char *scr_nodes[] = {
        "/sys/class/oplus_chg/battery/screen_off_chg_enable",
        "/sys/class/oplus_chg/battery/screen_off_slow_chg",
        "/sys/class/oplus_chg/common/screen_off_chg",
        "/sys/class/power_supply/battery/screen_off_chg",
        "/sys/devices/platform/charger/screen_off_throttle",

        "/sys/class/oplus_chg/battery/night_chg_enable",
        "/sys/class/oplus_chg/common/night_chg_enable",
        NULL
    };
    for (int i = 0; scr_nodes[i]; i++) {
        if (access(scr_nodes[i], W_OK)) continue;
        if (rd_int(scr_nodes[i]) != 0) wr_int(scr_nodes[i], 0);
    }

    {
        static int s_sconfig_was_zero = 1;
        const char *scfg = "/sys/class/thermal/thermal_message/sconfig";
        if (!access(scfg, W_OK)) {
            int v = rd_int(scfg);
            if (v > 0) {
                wr_int(scfg, 0);

                if (s_sconfig_was_zero) {
                    if (!access("/sys/class/power_supply/usb/apsd_rerun", W_OK))
                        wr_int("/sys/class/power_supply/usb/apsd_rerun", 1);
                }
                s_sconfig_was_zero = 0;
            } else {
                s_sconfig_was_zero = 1;
            }
        }
    }

    static const char *low_soc_va[] = {
        "/proc/oplus-votable/LOW_SOC/force_active",
        "/proc/oplus-votable/TRICKLE_CHG/force_active",
        "/proc/oplus-votable/LOW_CAPACITY/force_active",
        "/proc/oplus-votable/PRE_CHG/force_active",
        "/proc/oplus-votable/LOW_POWER/force_active",
        NULL
    };
    for (int i = 0; low_soc_va[i]; i++) {
        if (access(low_soc_va[i], W_OK)) continue;
        if (rd_int(low_soc_va[i]) != 0) wr_int(low_soc_va[i], 0);
    }

    {
        const char *siop = "/sys/class/power_supply/battery/siop_level";
        if (!access(siop, W_OK))
            if (rd_int(siop) != 100) wr_int(siop, 100);
    }

    static const char *cool_nodes[] = {
        "/sys/class/oplus_chg/battery/cool_down",
        "/sys/class/oplus_chg/battery/chg_cool_down",
        "/sys/class/oplus_chg/common/cool_down",
        "/sys/class/oplus_chg/battery/slow_chg_enable",
        "/sys/class/oplus_chg/common/slow_chg_enable",
        "/sys/class/oplus_chg/common/chg_cycle_status",
        "/sys/class/oplus_chg/battery/chg_cycle_enable",
        "/sys/class/power_supply/battery/charge_control_limit",
        "/sys/class/power_supply/battery/restricted_charging",
        "/sys/class/power_supply/battery/chg_control_limit_max",
        "/sys/class/power_supply/battery/lim_charge",
        "/sys/class/mi_battchg/mi_battchg/thermal_level",
        "/sys/class/mi_battchg/mi_battchg/thermal_enable",
        NULL
    };
    for (int i = 0; cool_nodes[i]; i++) {
        if (access(cool_nodes[i], W_OK)) continue;
        if (rd_int(cool_nodes[i]) != 0) wr_int(cool_nodes[i], 0);
    }

    static const char *cur_nodes[] = {
        "/sys/class/oplus_chg/common/wired_icl_votable",
        "/sys/class/oplus_chg/common/wired_fcc_votable",
        NULL
    };
    for (int i = 0; cur_nodes[i]; i++) {
        if (access(cur_nodes[i], W_OK)) continue;
        int v = rd_int(cur_nodes[i]);
        if (v >= 0 && v < 60000000) wr_int(cur_nodes[i], 65000000);
    }

    static const char *va_release[] = {
        "/proc/oplus-votable/CHG_SUSPEND/force_active",
        "/proc/oplus-votable/COOL_DOWN/force_active",
        "/proc/oplus-votable/WIRED_CHARGE_DONE/force_active",
        "/proc/oplus-votable/CHG_DISABLE/force_active",
        NULL
    };
    for (int i = 0; va_release[i]; i++) {
        if (access(va_release[i], W_OK)) continue;
        if (rd_int(va_release[i]) != 0) wr_int(va_release[i], 0);
    }

    if (g_vooc_handshake_guard > 0) {
        if (--g_vooc_handshake_guard == 0)
            charging_boost();
    } else {
        static const struct { const char *val; const char *active; } va_force[] = {
            { "/proc/oplus-votable/FCC/force_val",
              "/proc/oplus-votable/FCC/force_active" },
            { "/proc/oplus-votable/ICL/force_val",
              "/proc/oplus-votable/ICL/force_active" },
            { "/proc/oplus-votable/WIRED_CURR_CTRL/force_val",
              "/proc/oplus-votable/WIRED_CURR_CTRL/force_active" },
            { NULL, NULL }
        };
        for (int i = 0; va_force[i].val; i++) {
            if (access(va_force[i].val,    W_OK)) continue;
            if (access(va_force[i].active, W_OK)) continue;
            if (rd_int(va_force[i].active) != 1) {
                wr_int(va_force[i].val,    65000);
                wr_int(va_force[i].active, 1);
            } else {
                int v = rd_int(va_force[i].val);
                if (v >= 0 && v < 60000) wr_int(va_force[i].val, 65000);
            }
        }
    }
}


static const char *THERMAL_CFG_PATHS[] = {

    "/vendor/etc/thermal-engine.conf",
    "/vendor/etc/thermal-engine-v2.conf",
    "/vendor/etc/thermal-engine.v3.conf",
    "/vendor/etc/thermal-engine.v4.conf",
    "/vendor/etc/thermal-engine.v5.conf",

    "/vendor/etc/init/android.hardware.thermal-service.qti.rc",
    "/vendor/etc/init/init_thermal-engine-v2.rc",

    "/odm/etc/ThermalServiceConfig/sys_thermal_config.xml",
    "/odm/etc/temperature_profile/sys_thermal_control_config.xml",
    "/odm/etc/temperature_profile/sys_thermal_control_config_ext.xml",
    "/odm/etc/temperature_profile/sys_high_temp_protect_OPPO_24811.xml",
    "/odm/etc/temperature_profile/sys_thermal_zoom_window_restrict_list.xml",

    "/odm/etc/horae/horae_target.conf",
    NULL
};

static void ensure_fake_empty(void)
{
    if (!access(FAKE_EMPTY, F_OK)) return;
    mkdirp(PID_DIR);
    int fd = open(FAKE_EMPTY, O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC, 0644);
    if (fd >= 0) close(fd);
}

static int tc_is_mounted(const char *path)
{
    for (int i = 0; i < g_tc_nmounted; i++)
        if (!strcmp(g_tc_mounted[i], path)) return 1;
    return 0;
}

static void mount_thermal_configs(void)
{
    ensure_fake_empty();
    for (int i = 0; THERMAL_CFG_PATHS[i]; i++) {
        if (access(THERMAL_CFG_PATHS[i], F_OK)) continue;
        if (tc_is_mounted(THERMAL_CFG_PATHS[i])) continue;
        if (mount(FAKE_EMPTY, THERMAL_CFG_PATHS[i], NULL, MS_BIND, NULL) == 0) {
            if (g_tc_nmounted < MAX_TC_MOUNTS)
                strncpy(g_tc_mounted[g_tc_nmounted++], THERMAL_CFG_PATHS[i], MAX_PATH-1);
        }
    }

    run_async(
        "rm -f /data/vendor/thermal/* 2>/dev/null;"
        "rm -f /data/thermal/config/* 2>/dev/null"
    );
}

static void umount_thermal_configs(void)
{
    for (int i = 0; i < g_tc_nmounted; i++)
        unmount_all(g_tc_mounted[i]);
    for (int i = 0; THERMAL_CFG_PATHS[i]; i++)
        unmount_all(THERMAL_CFG_PATHS[i]);
    g_tc_nmounted = 0;
}

static void enforce_thermal_off(void)
{

    const char *t0[]={
        "/sys/class/thermal/thermal_message/board_sensor_temp_ratio",
        "/proc/oplus_temp/oplus_thermal_enable",
        "/proc/oplus_temp/disable_thermal",
        "/sys/module/msm_thermal/parameters/enabled",
        "/sys/module/msm_thermal/core_control/enabled",
        "/sys/kernel/msm_thermal/enabled",
        "/sys/power/pnpmgr/thermal/thermal_final_cpu",
        "/sys/power/pnpmgr/thermal/thermal_limit_cpu",
        NULL};
    for(int i=0;t0[i];i++) if(!access(t0[i],W_OK)) wr_int(t0[i],0);
    if(charge_nodes_enabled() &&
       !access("/sys/class/mi_battchg/mi_battchg/thermal_enable",W_OK))
        wr_int("/sys/class/mi_battchg/mi_battchg/thermal_enable",0);

    const char *t99[]={
        "/sys/kernel/fpsgo/fbt/limit_temp_dif",
        "/sys/kernel/fpsgo/fbt/limit_temp",
        NULL};
    for(int i=0;t99[i];i++) if(!access(t99[i],W_OK)) wr_int(t99[i],99);

    const char *tdis[]={
        "/sys/devices/platform/soc/soc:qcom,bcl-monitor/mode",
        "/sys/devices/platform/soc/soc:qcom,bcl/mode",
        NULL};
    for(int i=0;tdis[i];i++) if(!access(tdis[i],W_OK)) wr_str(tdis[i],"disable\n");



    {
        DIR *pd = opendir("/sys/devices/system/cpu/cpufreq");
        if (pd) {
            struct dirent *pe;
            while ((pe = readdir(pd))) {
                if (strncmp(pe->d_name, "policy", 6)) continue;
                char mxp[MAX_PATH], sfp[MAX_PATH];
                snprintf(mxp, sizeof(mxp),
                    "/sys/devices/system/cpu/cpufreq/%s/cpuinfo_max_freq", pe->d_name);
                snprintf(sfp, sizeof(sfp),
                    "/sys/devices/system/cpu/cpufreq/%s/scaling_max_freq", pe->d_name);
                if (access(mxp, R_OK) || access(sfp, W_OK)) continue;
                int maxf = rd_int(mxp);
                if (maxf > 0) wr_int(sfp, maxf);
            }
            closedir(pd);
        }
    }
}

static void thermal_kill(void)
{
    run_async(
        "stop thermal-engine 2>/dev/null;"
        "stop vendor.thermal-engine 2>/dev/null;"
        "stop vendor.thermal_manager 2>/dev/null;"
        "stop thermald 2>/dev/null;"
        "stop oppo_theias 2>/dev/null;"


        "stop vendor.oplus.ormsHalService-aidl-defaults 2>/dev/null;"
        "setprop init.svc.thermal-engine stopped;"
        "setprop init.svc.android.thermal-hal stopped;"
        "setprop init.svc.oppo_theias stopped;"

        "setprop init.svc.vendor.oplus.ormsHalService-aidl-default stopped;"
        "setprop init.svc.orms-hal-1-0 stopped;"
        "setprop ro.oplus.audio.thermal_control 0;"
        "setprop ro.oplus.radio.hide_nr_switch 0;"
        "setprop oplus.dex.tempcontrol false;"
        "setprop dalvik.vm.dexopt.thermal-cutoff 0;"
        "setprop sys.thermal.enable false;"
        "setprop persist.vendor.enable.cpulimit false;"
        "setprop persist.sys.oplus.wifi.sla.game_high_temperature '';"
        "setprop persist.sys.environment.temp 60;"
        "setprop gputuner_switch true;"
        "setprop persist.sys.ui.hw 1;"
        "setprop sys.oppo.high.performance 1;"
        "setprop sys.enable.hypnus 0"
    );

    apply_horae_whitelist();
    clear_cooling_devs();
}

static void restore_thermal_stack(void)
{
    umount_thermal_configs();
    run_async(
        "setprop sys.thermal.enable true 2>/dev/null;"
        "setprop persist.vendor.enable.cpulimit true 2>/dev/null;"
        "start thermal-engine 2>/dev/null;"
        "start vendor.thermal-engine 2>/dev/null;"
        "start vendor.thermal_manager 2>/dev/null;"
        "start thermald 2>/dev/null;"
        "start oppo_theias 2>/dev/null;"
        "start vendor.oplus.ormsHalService-aidl-defaults 2>/dev/null;"
        "start vendor.thermal-hal-2-0 2>/dev/null;"
        "start vendor.thermal-hal-2-1 2>/dev/null;"
        "start android.thermal-hal 2>/dev/null"
    );
    refresh_horae_policy();
}

static void d1x00_write_files(void)
{


    int vendor_rw = 0;
    if(mount(NULL, "/system/vendor", NULL, MS_REMOUNT|MS_NOATIME, NULL) == 0){
        vendor_rw = 1;
    } else if(mount(NULL, "/vendor", NULL, MS_REMOUNT|MS_NOATIME, NULL) == 0){
        vendor_rw = 1;
    }

    if(vendor_rw){
        mkdirp("/system/vendor/etc/.tp");
        write_blob("/system/vendor/etc/power_app_cfg.xml",g_power_app_cfg,g_power_app_cfg_len);
        write_blob("/system/vendor/etc/powercontable.xml",g_powercontable,g_powercontable_len);
        write_blob("/system/vendor/etc/powerscntbl.xml",  g_powerscntbl,  g_powerscntbl_len);
        if(!access("/odm/etc/powerhal",F_OK)){
            write_blob("/odm/etc/powerhal/power_app_cfg.xml",g_power_app_cfg,g_power_app_cfg_len);
            write_blob("/odm/etc/powerhal/powercontable.xml",g_powercontable,g_powercontable_len);
            write_blob("/odm/etc/powerhal/powerscntbl.xml",  g_powerscntbl,  g_powerscntbl_len);
        }
        write_blob("/system/vendor/etc/.tp/.ht120.mtc",        g_ht120_mtc,       g_ht120_mtc_len);
        write_blob("/system/vendor/etc/.tp/thermal.conf",      g_thermal_conf,    g_thermal_conf_len);
        write_blob("/system/vendor/etc/.tp/thermal.off.conf",  g_thermal_off_conf,g_thermal_off_conf_len);
        write_blob("/system/vendor/etc/.tp/.thermal_policy_08",g_thermal_policy,  g_thermal_policy_len);

        mount(NULL, "/system/vendor", NULL, MS_REMOUNT|MS_RDONLY, NULL);
        mount(NULL, "/vendor",        NULL, MS_REMOUNT|MS_RDONLY, NULL);
    }

    if (!access("/sys/module/cpufreq_bouncing/parameters/enable", W_OK)) {
        wr_int("/sys/module/cpufreq_bouncing/parameters/enable", 0);
        chmod("/sys/module/cpufreq_bouncing/parameters/enable", 0444);
    }

    if (!access("/sys/kernel/msm_performance/parameters/cpu_max_freq", W_OK)) {
        wr_str("/sys/kernel/msm_performance/parameters/cpu_max_freq",
               "0:9999999 1:9999999 2:9999999 3:9999999 "
               "4:9999999 5:9999999 6:9999999 7:9999999\n");
        chmod("/sys/kernel/msm_performance/parameters/cpu_max_freq", 0444);
    }
}

static void mtk_unlock(void)
{
    if(access("/proc/ppm/enabled",W_OK)) return;
    wr_int("/proc/ppm/enabled",1);
    const char *pols[]={
        "/proc/ppm/policy/hard_userlimit_cpu_freq",
        "/proc/ppm/policy/hard_userlimit_freq_limit_by_others",NULL};
    for(int i=0;pols[i];i++){
        if(access(pols[i],W_OK)) continue;
        int fd=open(pols[i],O_WRONLY|O_CLOEXEC);
        if(fd>=0){write(fd,"0 -1\n",5);write(fd,"1 -1\n",5);close(fd);}
    }
    wr_int("/sys/kernel/fpsgo/fbt/thrm_temp_th",100);
    wr_str("/sys/kernel/fpsgo/fbt/thrm_limit_cpu","-1\n");
    wr_str("/sys/kernel/fpsgo/fbt/thrm_sub_cpu",  "-1\n");
    int fd=open("/proc/gpufreq/gpufreq_limit_table",O_WRONLY|O_CLOEXEC);
    if(fd>=0){
        for(int i=3;i<=6;i++){char b[16];int n=snprintf(b,sizeof(b),"%d 0 0\n",i);write(fd,b,n);}
        close(fd);
    }
}

static void detect_bl_path(void)
{
    static const char *c[] = {
        "/sys/class/backlight/panel0-backlight/brightness",
        "/sys/class/leds/lcd-backlight/brightness",
        "/sys/class/backlight/lcd-backlight/brightness",
        "/sys/class/backlight/oplus_bl0/brightness",
        "/sys/class/backlight/oplus_bl/brightness",
        NULL
    };
    for (int i = 0; c[i]; i++) {
        if (!access(c[i], R_OK)) {
            strncpy(g_blpath, c[i], MAX_PATH-1);
            return;
        }
    }
}

static void pid_write(void)
    {FILE *f=fopen(g_pidpath,"w");if(!f)return;fprintf(f,"%d\n",(int)getpid());fclose(f);}
static int  pid_read(void) {return rd_int(g_pidpath);}

static void release_charging_forces(void)
{
    const char *va_rel[] = {
        "/proc/oplus-votable/FCC/force_active",
        "/proc/oplus-votable/ICL/force_active",
        "/proc/oplus-votable/WIRED_CURR_CTRL/force_active",
        "/proc/oplus-votable/GAUGE_UPDATE/force_active",
        "/proc/oplus-votable/CHG_DISABLE/force_active",
        "/proc/oplus-votable/CHG_SUSPEND/force_active",
        "/proc/oplus-votable/WIRED_CHARGE_DONE/force_active",
        "/proc/oplus-votable/COOL_DOWN/force_active",
        "/proc/oplus-votable/LOW_SOC/force_active",
        "/proc/oplus-votable/TRICKLE_CHG/force_active",
        "/proc/oplus-votable/LOW_CAPACITY/force_active",
        "/proc/oplus-votable/PRE_CHG/force_active",
        "/proc/oplus-votable/LOW_POWER/force_active",
        NULL
    };
    for (int i = 0; va_rel[i]; i++)
        if (!access(va_rel[i], W_OK)) wr_int(va_rel[i], 0);
}

static void cleanup(void)
{
    restore_thermal_stack();
    clear_emul();
    unbind_batt_temp();
    unbind_cycle_count();
    unbind_cap();

    release_charging_forces();

    if (!access("/sys/class/power_supply/battery/siop_level", W_OK))
        wr_int("/sys/class/power_supply/battery/siop_level", 100);

    const char *vendor_files[] = {
        "/system/vendor/etc/power_app_cfg.xml",
        "/system/vendor/etc/powercontable.xml",
        "/system/vendor/etc/powerscntbl.xml",
        "/system/vendor/etc/.tp/.ht120.mtc",
        "/system/vendor/etc/.tp/thermal.conf",
        "/system/vendor/etc/.tp/thermal.off.conf",
        "/system/vendor/etc/.tp/.thermal_policy_08",
        "/odm/etc/powerhal/power_app_cfg.xml",
        "/odm/etc/powerhal/powercontable.xml",
        "/odm/etc/powerhal/powerscntbl.xml",
        NULL
    };
    for (int i = 0; vendor_files[i]; i++)
        umount2(vendor_files[i], MNT_DETACH);

    run_async(
        "dumpsys battery reset;"
        "if getprop persist.sys.oiface.enable >/dev/null 2>&1; then"
        "  setprop persist.sys.horae.enable 1; start horae; fi"
    );

    unlink(g_pidpath);
}

static int read_master_switch(void)
{
    int fd = open(g_masterpath, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 1;
    char buf[8]; int n = (int)read(fd, buf, 7); close(fd);
    if (n <= 0) return 1;
    buf[n] = '\0';
    return atoi(buf) != 0;
}

static int read_horae_switch(void)
{
    int fd = open(g_horaespath, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 1;
    char buf[8]; int n = (int)read(fd, buf, 7); close(fd);
    if (n <= 0) return 1;
    buf[n] = '\0';
    int v = atoi(buf);
    return (v == 0) ? 0 : 1;
}

static void refresh_horae_policy(void)
{
    g_horae_sw = read_horae_switch();
    g_horae_apply_last = -1;
    apply_horae_whitelist();
}

static void get_foreground_pkg(char *buf, size_t n)
{
    buf[0] = '\0';
    FILE *fp = popen(
        "dumpsys window 2>/dev/null"
        " | grep -om1 'mCurrentFocus=Window{[^}]*}'"
        " | grep -oE ' [a-zA-Z][a-zA-Z0-9_.]+/' | head -1 | tr -d '/ '",
        "r");
    if (!fp) return;
    if (fgets(buf, (int)n, fp))
        buf[strcspn(buf, " \t\r\n")] = '\0';
    pclose(fp);
}

static void apply_horae_whitelist(void)
{
    if (g_horae_sw == 0) {
        if (g_horae_apply_last != 1) {
            run_async("setprop ctl.start horae");
            g_horae_apply_last = 1;
        }
        return;
    }

    FILE *wf = fopen(g_horaebai_path, "r");
    if (!wf) {
        if (g_horae_apply_last != 0) {
            run_async("setprop ctl.stop horae");
            g_horae_apply_last = 0;
        }
        return;
    }

    char line[256];
    int has_pkg = 0, matched = 0, fg_fetched = 0;
    char fg[256] = {0};

    while (fgets(line, sizeof(line), wf)) {
        int l = (int)strlen(line);
        while (l > 0 && (unsigned char)line[l-1] <= ' ') line[--l] = '\0';
        if (l == 0) continue;
        has_pkg = 1;
        if (!fg_fetched) { get_foreground_pkg(fg, sizeof(fg)); fg_fetched = 1; }
        if (fg[0] && strcmp(fg, line) == 0) { matched = 1; break; }
    }
    fclose(wf);

    int want = (has_pkg && matched) ? 1 : 0;
    if (want != g_horae_apply_last) {
        run_async(want ? "setprop ctl.start horae" : "setprop ctl.stop horae");
        g_horae_apply_last = want;
    }
}

static int read_jzwz_switch(void)
{
    int fd = open(g_jzwzpath, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;
    char buf[8]; int n = (int)read(fd, buf, 7); close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    return (atoi(buf) == 1) ? 1 : 0;
}

static int read_cdms_switch(void)
{
    int fd = open(g_cdmspath, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;
    char buf[8]; int n = (int)read(fd, buf, 7); close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    return (atoi(buf) == 1) ? 1 : 0;
}

static int check_cdz_file(void)
{
    int fd = open(BATT_SYS"/status", O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;
    char buf[32] = {0};
    int n = (int)read(fd, buf, 31);
    close(fd);
    if (n <= 0) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    if (strncasecmp(buf, "Charging", 8) == 0) return 1;
    if (strncasecmp(buf, "Full",     4) == 0) return 1;
    return 0;
}

static void master_on(void)
{
    discover_zones();
    mount_thermal_configs();
    thermal_kill();
    sleep(2);
    thermal_kill();
    charging_boost();
    enforce_thermal_off();
    disable_zone_governors();
    mtk_unlock();
    roll_dynamic_temps();
    mount_batt();

    g_rf_emul_delay = 30;
    apply_emul();
}

static void master_off(void)
{
    reset_batt_charge_ramp();
    g_vooc_handshake_guard = 0;
    restore_thermal_stack();
    clear_emul();
    unbind_batt_temp();
    unbind_cycle_count();
    release_charging_forces();
    run_async("dumpsys battery reset");
}

#define INTEGRITY_LOG \
    AATS_ROOT "/\xe6\x97\xa5\xe5\xbf\x97.log"

static void integrity_log(const char *fmt, ...)
{

    {
        FILE *cf = fopen(MODULE_PROP, "r");
        int enabled = 0;
        if (cf) {
            char ln[64];
            while (fgets(ln, sizeof(ln), cf)) {
                int l = (int)strlen(ln);
                while (l > 0 && (unsigned char)ln[l-1] <= ' ') ln[--l] = '\0';
                if (strcmp(ln, "rz=1") == 0) { enabled = 1; break; }
            }
            fclose(cf);
        }
        if (!enabled) return;
    }

    int fd = open(INTEGRITY_LOG,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return;


    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);


    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);


    char line[1088];
    int  len = snprintf(line, sizeof(line), "[%s] %s\n", ts, msg);
    if (len > 0 && len < (int)sizeof(line))
        write(fd, line, (size_t)len);

    close(fd);
}


static int read_prop_field(const char *path, const char *key,
                           char *out, size_t outlen)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t klen = strlen(key);
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        int l = (int)strlen(line);
        while (l > 0 && (unsigned char)line[l-1] <= ' ') line[--l] = '\0';
        if (strncmp(line, key, klen) != 0 || line[klen] != '=') continue;
        strncpy(out, line + klen + 1, outlen - 1);
        out[outlen - 1] = '\0';
        found = 1;
        break;
    }
    fclose(f);
    return found;
}


static int run_capture(const char *cmd, char *out, size_t outlen)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    size_t n = fread(out, 1, outlen - 1, fp);
    pclose(fp);
    out[n] = '\0';

    while (n > 0 && (unsigned char)out[n-1] <= ' ') out[--n] = '\0';
    return (int)n;
}

#define DONATE_PNG  AATS_ROOT "/webroot/\xe6\x8d\x90\xe8\xb5\xa0.png"

#define DEV_MODE_FILE  AATS_ROOT "/\xe5\xbc\x80\xe5\x8f\x91\xe8\x80\x85\xe6\xa8\xa1\xe5\xbc\x8f"
#define DEV_MODE_CODE  "828384"

static void repair_module_prop(void)
{

    FILE *fr = fopen(MODULE_PROP, "r");
    char buf[8192] = {0};
    if (fr) { fread(buf, 1, sizeof(buf) - 1, fr); fclose(fr); }

    char tmppath[MAX_PATH];
    snprintf(tmppath, sizeof(tmppath), MODULE_PROP ".tmp");
    FILE *fw = fopen(tmppath, "w");
    if (!fw) return;

    int saw_desc = 0, saw_name = 0, saw_author = 0;
    char *p = buf;
    while (*p) {
        char *nl  = strchr(p, '\n');
        int   len = nl ? (int)(nl - p + 1) : (int)strlen(p);
        char  line[512];
        int   cp  = len < (int)sizeof(line)-1 ? len : (int)sizeof(line)-1;
        memcpy(line, p, cp); line[cp] = '\0';

        int ll = (int)strlen(line);
        while (ll > 0 && (line[ll-1]=='\n'||line[ll-1]=='\r')) line[--ll] = '\0';

        if (strncmp(line, "description=", 12) == 0) {

            fprintf(fw, "description=\xe6\xa8\xa1\xe5\x9d\x97\xe5\xb7\xb2\xe6\x8d\x9f"
                        "\xe5\x9d\x8f\xe6\x88\x96\xe8\xa2\xab\xe7\xaf\xa1\xe6\x94\xb9\n");
            saw_desc   = 1;
        } else if (strncmp(line, "name=", 5) == 0) {
            fprintf(fw, "name=\xe6\xa8\xa1\xe5\x9d\x97\xe5\xb7\xb2\xe6\x8d\x9f"
                        "\xe5\x9d\x8f\xe6\x88\x96\xe8\xa2\xab\xe7\xaf\xa1\xe6\x94\xb9\n");
            saw_name   = 1;
        } else if (strncmp(line, "author=", 7) == 0) {

            fprintf(fw, "author=\xe9\x85\xb7\xe5\xae\x89@\xe5\x82\xbb\xe7\x93\x9c"
                        "\xe6\x88\x91\xe7\x88\xb1\xe4\xbd\xa0\xe5\x91\x80\xe5\x96\xb5\n");
            saw_author = 1;
        } else {
            fprintf(fw, "%s\n", line);
        }
        p += len;
        if (!nl) break;
    }

    if (!saw_desc)
        fprintf(fw, "description=\xe6\xa8\xa1\xe5\x9d\x97\xe5\xb7\xb2\xe6\x8d\x9f"
                    "\xe5\x9d\x8f\xe6\x88\x96\xe8\xa2\xab\xe7\xaf\xa1\xe6\x94\xb9\n");
    if (!saw_name)
        fprintf(fw, "name=\xe6\xa8\xa1\xe5\x9d\x97\xe5\xb7\xb2\xe6\x8d\x9f"
                    "\xe5\x9d\x8f\xe6\x88\x96\xe8\xa2\xab\xe7\xaf\xa1\xe6\x94\xb9\n");
    if (!saw_author)
        fprintf(fw, "author=\xe9\x85\xb7\xe5\xae\x89@\xe5\x82\xbb\xe7\x93\x9c"
                    "\xe6\x88\x91\xe7\x88\xb1\xe4\xbd\xa0\xe5\x91\x80\xe5\x96\xb5\n");
    fclose(fw);
    rename(tmppath, MODULE_PROP);
}

static int verify_module_integrity(void)
{

    {
        FILE *df = fopen(DEV_MODE_FILE, "r");
        if (df) {
            char code[16] = {0};
            size_t n = fread(code, 1, sizeof(code) - 1, df);
            fclose(df);
            while (n > 0 && (unsigned char)code[n-1] <= ' ') code[--n] = '\0';
            if (strcmp(code, DEV_MODE_CODE) == 0)
                return 1;
        }
    }


    if (access(AATS_ROOT "/webroot", F_OK) == 0) {


        char hash_actual[128] = {0};
        if (!run_capture(
                "sha256sum " INDEX_HTML " 2>/dev/null | awk '{print $1}'",
                hash_actual, sizeof(hash_actual))) {
            integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                          " | \xe9\xa1\xb9\xe7\x9b\xae: index.html sha256 \xe8\xae\xa1\xe7\xae\x97\xe5\xa4\xb1\xe8\xb4\xa5"
                          " (\xe6\x96\x87\xe4\xbb\xb6\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8\xe6\x88\x96\xe6\x97\xa0\xe6\xb3\x95\xe8\xaf\xbb\xe5\x8f\x96)");
            return 0;
        }


        char hash_expected[128] = {0};
        if (!read_prop_field(MODULE_PROP, "AaTempSpoof",
                             hash_expected, sizeof(hash_expected))) {
            integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                          " | \xe9\xa1\xb9\xe7\x9b\xae: module.prop \xe7\xbc\xba\xe5\xb0\x91"
                          " AaTempSpoof= \xe5\xad\x97\xe6\xae\xb5");
            return 0;
        }
        if (strcmp(hash_actual, hash_expected) != 0) {
            integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                          " | \xe9\xa1\xb9\xe7\x9b\xae: index.html sha256 \xe4\xb8\x8d\xe7\xac\xa6"
                          " | \xe5\xae\x9e\xe9\x99\x85=%s \xe9\xa2\x84\xe6\x9c\x9f=%s",
                          hash_actual, hash_expected);
            return 0;
        }



        if (access(DONATE_PNG, F_OK) != 0) {
            integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                          " | \xe9\xa1\xb9\xe7\x9b\xae: "
                          "\xe6\x8d\x90\xe8\xb5\xa0.png \xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"
                          " (%s)", DONATE_PNG);
            return 0;
        }

    }


    char name_val[256] = {0};
    if (!read_prop_field(MODULE_PROP, "name", name_val, sizeof(name_val))) {
        integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                      " | \xe9\xa1\xb9\xe7\x9b\xae: module.prop \xe7\xbc\xba\xe5\xb0\x91 name= \xe5\xad\x97\xe6\xae\xb5");
        return 0;
    }

    if (!strstr(name_val, "AaTempSpoof") &&
        !strstr(name_val, "\xe4\xbc\xaa\xe8\xa3\x85\xe6\x8e\xa7\xe5\x88\xb6\xe5\x8f\xb0")) {
        integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                      " | \xe9\xa1\xb9\xe7\x9b\xae: name= \xe5\x80\xbc\xe4\xb8\x8d\xe5\x90\x88\xe6\xb3\x95"
                      " | \xe5\xae\x9e\xe9\x99\x85=\"%s\"", name_val);
        return 0;
    }


    char author_val[256] = {0};
    if (!read_prop_field(MODULE_PROP, "author", author_val, sizeof(author_val))) {
        integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                      " | \xe9\xa1\xb9\xe7\x9b\xae: module.prop \xe7\xbc\xba\xe5\xb0\x91 author= \xe5\xad\x97\xe6\xae\xb5");
        return 0;
    }
    if (strcmp(author_val,
               "\xe9\x85\xb7\xe5\xae\x89@\xe5\x82\xbb\xe7\x93\x9c"
               "\xe6\x88\x91\xe7\x88\xb1\xe4\xbd\xa0\xe5\x91\x80\xe5\x96\xb5") != 0) {
        integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                      " | \xe9\xa1\xb9\xe7\x9b\xae: author= \xe5\x80\xbc\xe4\xb8\x8d\xe7\xac\xa6"
                      " | \xe5\xae\x9e\xe9\x99\x85=\"%s\"", author_val);
        return 0;
    }


    char upd_val[512] = {0};
    if (!read_prop_field(MODULE_PROP, "updateJson", upd_val, sizeof(upd_val))) {
        integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                      " | \xe9\xa1\xb9\xe7\x9b\xae: module.prop \xe7\xbc\xba\xe5\xb0\x91 updateJson= \xe5\xad\x97\xe6\xae\xb5");
        return 0;
    }
    if (strcmp(upd_val,
               "https://raw.githubusercontent.com/csbxd/"
               "AaTempSpoof/main/update.json") != 0) {
        integrity_log("\xe6\xa0\xa1\xe9\xaa\x8c\xe5\xa4\xb1\xe8\xb4\xa5"
                      " | \xe9\xa1\xb9\xe7\x9b\xae: updateJson= \xe5\x80\xbc\xe4\xb8\x8d\xe7\xac\xa6"
                      " | \xe5\xae\x9e\xe9\x99\x85=\"%s\"", upd_val);
        return 0;
    }

    return 1;
}

static void integrity_tamper_response(void)
{

    integrity_log("\xe7\xaf\xa1\xe6\x94\xb9\xe6\xa3\x80\xe6\xb5\x8b"
                  " | \xe5\xbc\x80\xe5\xa7\x8b\xe5\xa4\x84\xe7\xbd\xae: "
                  "\xe6\x89\xa7\xe8\xa1\x8c uninstall.sh");
    run_async(UNINSTALL_SH " 2>/dev/null");

    integrity_log("\xe5\xa4\x84\xe7\xbd\xae"
                  " | \xe5\x88\xa0\xe9\x99\xa4\xe7\x9b\xae\xe5\xbd\x95: "
                  AATS_ROOT "/webroot");
    run_async("rm -rf " AATS_ROOT "/webroot");

    integrity_log("\xe5\xa4\x84\xe7\xbd\xae"
                  " | \xe5\x88\xa0\xe9\x99\xa4\xe7\x9b\xae\xe5\xbd\x95: "
                  AATS_ROOT "/AaTempSpoof");
    run_async("rm -rf " AATS_ROOT "/AaTempSpoof");

    integrity_log("\xe5\xa4\x84\xe7\xbd\xae"
                  " | \xe4\xbf\xae\xe5\xa4\x8d module.prop "
                  "description/name/author \xe5\xad\x97\xe6\xae\xb5");
    repair_module_prop();

    integrity_log("\xe5\xa4\x84\xe7\xbd\xae\xe5\xae\x8c\xe6\x88\x90");
}

int main(int argc, char *argv[])
{
    int do_start=0,do_stop=0,do_status=0;
    for(int i=1;i<argc;i++){
        if     (!strcmp(argv[i],"--start"))  do_start =1;
        else if(!strcmp(argv[i],"--stop"))   do_stop  =1;
        else if(!strcmp(argv[i],"--status")) do_status=1;
    }
    mkdirp(DATA_DIR);
    mkdirp(PID_DIR);

    snprintf(g_cfgpath,      sizeof(g_cfgpath),      DATA_DIR "/" CFG_NAME);
    snprintf(g_pidpath,      sizeof(g_pidpath),      PID_DIR  "/daemon.pid");
    snprintf(g_fakepath,     sizeof(g_fakepath),     PID_DIR  "/fake_batt_temp");
    snprintf(g_fakeuevpath,  sizeof(g_fakeuevpath),  PID_DIR  "/fake_uevent");
    snprintf(g_fakebccpath,  sizeof(g_fakebccpath),  PID_DIR  "/fake_bcc_parms");
    snprintf(g_fakecyclepath,sizeof(g_fakecyclepath),PID_DIR  "/fake_cycle_count");
    snprintf(g_fakecappath,  sizeof(g_fakecappath),  PID_DIR  "/fake_capacity");
    snprintf(g_masterpath,   sizeof(g_masterpath),   DATA_DIR "/总开关.txt");
    snprintf(g_horaespath,   sizeof(g_horaespath),   DATA_DIR "/horae.txt");
    snprintf(g_horaebai_path,sizeof(g_horaebai_path),DATA_DIR "/horaebai.txt");
    snprintf(g_cdmspath,     sizeof(g_cdmspath),     DATA_DIR "/cdms.txt");
    snprintf(g_cdzpath,      sizeof(g_cdzpath),      DATA_DIR "/cdz");
    snprintf(g_jzwzpath,     sizeof(g_jzwzpath),     DATA_DIR "/jzwz.txt");

    if(do_status){
        int pid=pid_read(),alive=(pid>0&&kill(pid,0)==0);
        puts(alive?"running":"stopped"); return alive?0:1;
    }
    if(do_stop){
        int pid=pid_read();
        if(pid>0){
            kill(pid,SIGTERM);
            for(int i=0;i<20;i++){usleep(100000);if(kill(pid,0))break;}
            if(!kill(pid,0)) kill(pid,SIGKILL);
        }
        restore_thermal_stack();
        discover_zones(); clear_emul(); unbind_batt_temp();
        release_charging_forces();
        run_async("dumpsys battery reset");
        usleep(300000);
        unlink(g_pidpath); return 0;
    }
    if(!do_start) return 1;

    {int pid=pid_read();if(pid>0&&kill(pid,0)==0){kill(pid,SIGHUP);return 0;}}

    {pid_t c=fork();if(c<0)return 1;if(c>0)return 0;}
    setsid();
    {pid_t c=fork();if(c<0)return 1;if(c>0)return 0;}
    {int nfd=open("/dev/null",O_RDWR|O_CLOEXEC);
     if(nfd>=0){dup2(nfd,0);dup2(nfd,1);dup2(nfd,2);close(nfd);}}

    signal(SIGTERM,on_term); signal(SIGINT,on_term); signal(SIGHUP,on_hup);
    pid_write();




    setpriority(PRIO_PROCESS, 0, 10);

    srand((unsigned)(time(NULL) ^ (getpid() << 16)));

    parse_config();



    g_jzwz      = read_jzwz_switch();
    g_jzwz_prev = g_jzwz;

    g_master = read_master_switch();
    g_master_prev = g_master;
    if (g_master) {
        g_cdms = read_cdms_switch();
        g_cdms_prev  = g_cdms;
        if (g_cdms) {

            g_cdz_active = check_cdz_file();
            g_cdz_prev   = g_cdz_active;
            if (g_cdz_active) {
                master_on();
            } else {
                master_off();
            }

        } else {
            master_on();
        }
    } else {

        g_horae_sw = read_horae_switch();
        apply_horae_whitelist();
    }

    g_horae_sw      = read_horae_switch();
    g_horae_sw_prev = g_horae_sw;
    d1x00_write_files();

    int ifd=inotify_init1(IN_NONBLOCK|IN_CLOEXEC),iwd=-1;
    if(ifd>=0){
        char dir[MAX_PATH]; strncpy(dir,g_cfgpath,MAX_PATH-1);
        char *sl=strrchr(dir,'/'); if(sl)*sl='\0';
        iwd=inotify_add_watch(ifd,dir,IN_CLOSE_WRITE|IN_MOVED_TO);
        if(iwd<0){close(ifd);ifd=-1;}
    }


    detect_bl_path();
    int blfd=-1, blwd=-1;
    if(g_blpath[0]){
        blfd=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
        if(blfd>=0){
            char bldir[MAX_PATH]; strncpy(bldir,g_blpath,MAX_PATH-1);
            char *sl=strrchr(bldir,'/'); if(sl)*sl='\0';
            blwd=inotify_add_watch(blfd,bldir,IN_MODIFY);
            if(blwd<0){close(blfd);blfd=-1;}
        }
    }


    int intfd     = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
    int int_wd_mod = -1, int_wd_web = -1;
    if (intfd >= 0) {

        int_wd_mod = inotify_add_watch(intfd, AATS_ROOT,
            IN_CLOSE_WRITE|IN_MOVED_TO|IN_MOVED_FROM|
            IN_CREATE|IN_DELETE|IN_ATTRIB);
        if (int_wd_mod < 0) { close(intfd); intfd = -1; }
    }
    if (intfd >= 0) {

        int_wd_web = inotify_add_watch(intfd, AATS_ROOT "/webroot",
            IN_CLOSE_WRITE|IN_MOVED_TO|IN_MOVED_FROM|
            IN_CREATE|IN_DELETE|IN_ATTRIB);

    }

    if (module_restore_requested())
        module_restore_response();
    if (!g_module_restore_started && !verify_module_integrity())
        integrity_tamper_response();



    static const char *XM_THERMAL_NODES[] = {
        "/sys/class/xm_power/charger/charger_thermal/wired_thermal_remove",
        "/sys/class/xm_power/charger/charger_thermal/wireless_thermal_remove",
        "/sys/class/qcom-battery/thermal_remove",
        NULL
    };
    int xmfd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);

    if (xmfd >= 0) {
        int any = 0;
        for (int i = 0; XM_THERMAL_NODES[i]; i++) {
            if (access(XM_THERMAL_NODES[i], W_OK)) continue;
            char xmdir[MAX_PATH];
            strncpy(xmdir, XM_THERMAL_NODES[i], MAX_PATH-1);
            char *sl = strrchr(xmdir, '/'); if (sl) *sl = '\0';

            if (inotify_add_watch(xmfd, xmdir, IN_MODIFY) >= 0)
                any = 1;
        }
        if (!any) { close(xmfd); xmfd = -1; }
    }

    int tick=0;
    long long next_dyn_roll_ms = 0;
    int spoof_prev_tick = 1;
    while(g_run){
        struct timeval tv = spoof_prev_tick ? (struct timeval){1,0} : (struct timeval){10,0};
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd=0;
        if(ifd>=0){FD_SET(ifd,&rfds);if(ifd>maxfd)maxfd=ifd;}
        if(blfd>=0 && spoof_prev_tick){FD_SET(blfd,&rfds);if(blfd>maxfd)maxfd=blfd;}
        else if(blfd>=0){char _eb[256];while(read(blfd,_eb,sizeof(_eb))>0){}}

        if(intfd>=0){FD_SET(intfd,&rfds);if(intfd>maxfd)maxfd=intfd;}

        if(xmfd>=0){FD_SET(xmfd,&rfds);if(xmfd>maxfd)maxfd=xmfd;}
        int ret = select(maxfd+1, &rfds, NULL, NULL, &tv);
        if(!g_run) break;

        g_master = read_master_switch();
        if (g_master != g_master_prev) {
            if (g_master) {
                g_cdms = read_cdms_switch();
                g_cdms_prev = g_cdms;
                if (g_cdms) {
                    g_cdz_active = check_cdz_file();
                    g_cdz_prev   = g_cdz_active;
                    if (g_cdz_active) master_on();
                    else master_off();

                } else {
                    master_on();
                }
            } else {
                master_off();
            }
            g_master_prev = g_master;
            tick = 0;
            next_dyn_roll_ms = 0;
        }


        g_horae_sw = read_horae_switch();
        g_horae_sw_prev = g_horae_sw;
        apply_horae_whitelist();


        g_jzwz = read_jzwz_switch();
        if (g_jzwz != g_jzwz_prev) {

            clear_emul();
            g_jzwz_prev = g_jzwz;
            discover_zones();
            if (g_master && (!g_cdms || g_cdz_active)) apply_emul();
        }


        if (g_master) {
            g_cdms = read_cdms_switch();
            if (g_cdms != g_cdms_prev) {
                if (g_cdms) {
                    g_cdz_active = check_cdz_file();
                    g_cdz_prev   = g_cdz_active;
                    if (g_cdz_active) master_on();
                    else master_off();
                } else {
                    master_on();
                    g_cdz_prev = -1;
                }
                g_cdms_prev = g_cdms;
                tick = 0;
                next_dyn_roll_ms = 0;
            }


            if (g_cdms) {
                g_cdz_active = check_cdz_file();
                if (g_cdz_active != g_cdz_prev) {
                    if (g_cdz_active) {
                        master_on();
                    } else {
                        master_off();
                    }
                    g_cdz_prev = g_cdz_active;
                    tick = 0;
                    next_dyn_roll_ms = 0;
                }
            }
        }


        int spoof_active = g_master && (!g_cdms || g_cdz_active);
        spoof_prev_tick = spoof_active;

        if(ret > 0 && ifd>=0 && FD_ISSET(ifd,&rfds)){
            char eb[512]; while(read(ifd,eb,sizeof(eb))>0){}
            usleep(200000); g_reload=1;
        }

        if(ret > 0 && intfd>=0 && FD_ISSET(intfd,&rfds)){
            char eb[512]; while(read(intfd,eb,sizeof(eb))>0){}
            if(module_restore_requested())
                module_restore_response();
            else if(!verify_module_integrity())
                integrity_tamper_response();
        }
        if(g_module_restore_started) break;


        if(ret > 0 && xmfd>=0 && FD_ISSET(xmfd,&rfds)){
            char eb[512]; while(read(xmfd,eb,sizeof(eb))>0){}
            if(spoof_active && charge_nodes_enabled()){
                for(int i=0;XM_THERMAL_NODES[i];i++){
                    if(access(XM_THERMAL_NODES[i],W_OK)) continue;
                    if(rd_int(XM_THERMAL_NODES[i])!=1) wr_int(XM_THERMAL_NODES[i],1);
                }
            }
        }
        if(g_reload){
            g_reload=0;
            parse_config();
            if(spoof_active) disable_zone_governors();
            next_dyn_roll_ms = 0;
            if(spoof_active){ update_fake_batt(); }
        }

        if(spoof_active){


            long long now_ms = monotonic_ms();
            if (next_dyn_roll_ms == 0 || now_ms >= next_dyn_roll_ms) {
                roll_dynamic_temps();
                if (g_en_batt) write_fake_batt_file();
                next_dyn_roll_ms = now_ms + DYN_ROLL_INTERVAL_MS;
            }
            refresh_fake_uevent();
            write_fake_bcc_file();
            refresh_cap_spoof();
            apply_emul();
            maintain_charging();
            tick++;
            if(g_rf_emul_delay > 0) g_rf_emul_delay--;

            {
                static int s_ac_prev  = -1;
                static int s_usb_prev = -1;
                int cur_ac  = rd_int("/sys/class/power_supply/ac/online");
                int cur_usb = rd_int("/sys/class/power_supply/usb/online");
                if(cur_ac  < 0) cur_ac  = 0;
                if(cur_usb < 0) cur_usb = 0;
                int was_online = (s_ac_prev > 0 || s_usb_prev > 0);
                int now_online = (cur_ac    > 0 || cur_usb    > 0);
                if(cur_ac != s_ac_prev || cur_usb != s_usb_prev){
                    if(s_ac_prev != -1 || s_usb_prev != -1){
                        if(!now_online && was_online){

                            g_vooc_handshake_guard = 0;
                            refresh_fake_uevent();
                            {
                                int _st=3;
                                { char _sb[32]={0};
                                  int _fd=open(BATT_SYS"/status",O_RDONLY|O_CLOEXEC);
                                  if(_fd>=0){int _n=(int)read(_fd,_sb,31);close(_fd);
                                    if(_n>0){_sb[strcspn(_sb,"\r\n")]='\0';
                                      if     (strncasecmp(_sb,"Charging",    8)==0) _st=2;
                                      else if(strncasecmp(_sb,"Full",        4)==0) _st=5;
                                      else if(strncasecmp(_sb,"Not charging",12)==0) _st=4;
                                    }
                                  }
                                }
                                int _lv = batt_level_for_dumpsys();
                                run_dumpsys_batt_update(1, batt_temp_for_dumpsys(), _st, _lv);
                            }

                            g_skip_dumpsys_set = 6;
                        } else if(now_online && !was_online){

                            refresh_fake_uevent();
                            {
                                int _st=3;
                                { char _sb[32]={0};
                                  int _fd=open(BATT_SYS"/status",O_RDONLY|O_CLOEXEC);
                                  if(_fd>=0){int _n=(int)read(_fd,_sb,31);close(_fd);
                                    if(_n>0){_sb[strcspn(_sb,"\r\n")]='\0';
                                      if     (strncasecmp(_sb,"Charging",    8)==0) _st=2;
                                      else if(strncasecmp(_sb,"Full",        4)==0) _st=5;
                                      else if(strncasecmp(_sb,"Not charging",12)==0) _st=4;
                                    }
                                  }
                                }
                                int _lv = batt_level_for_dumpsys();
                                run_dumpsys_batt_update(1, batt_temp_for_dumpsys(), _st, _lv);
                            }
                            g_vooc_handshake_guard = 16;
                            g_skip_dumpsys_set =
                                charge_type_keeps_vendor_plug(read_oplus_charge_type()) ? 0 : 20;
                        }
                    }
                    s_ac_prev  = cur_ac;
                    s_usb_prev = cur_usb;
                }
            }



            if(tick % 30 == 0 && g_en_batt){
                if(!batt_bind_alive()){
                    g_batt_bound = 0;
                    g_uev_bound  = 0;
                    g_bcc_bound  = 0;
                    g_cycle_bound= 0;
                    if(g_real_uev_fd >= 0){ close(g_real_uev_fd); g_real_uev_fd = -1; }
                    if(g_real_bcc_fd >= 0){ close(g_real_bcc_fd); g_real_bcc_fd = -1; }
                    update_fake_batt();
                }
            }

            dumpsys_batt_temp();

            if(tick % 10 == 0){
                clear_cooling_devs();
                disable_zone_governors();
                enforce_thermal_off();
                {
                    DIR *pd=opendir("/sys/devices/system/cpu/cpufreq");
                    if(pd){
                        struct dirent *pe;
                        while((pe=readdir(pd))){
                            if(strncmp(pe->d_name,"policy",6)) continue;
                            char mxp[MAX_PATH],sfp[MAX_PATH];
                            snprintf(mxp,sizeof(mxp),
                                "/sys/devices/system/cpu/cpufreq/%s/cpuinfo_max_freq",pe->d_name);
                            snprintf(sfp,sizeof(sfp),
                                "/sys/devices/system/cpu/cpufreq/%s/scaling_max_freq",pe->d_name);
                            if(access(mxp,R_OK)||access(sfp,W_OK)) continue;
                            int maxf=rd_int(mxp);
                            if(maxf>0){int cur=rd_int(sfp);if(cur<maxf) wr_int(sfp,maxf);}
                        }
                        closedir(pd);
                    }
                }
                if(tick % 30 == 0){
                    mount_thermal_configs();
                    thermal_kill();
                }
            }





            if(blfd>=0 && FD_ISSET(blfd,&rfds)){
                char eb[256]; while(read(blfd,eb,sizeof(eb))>0){}
                int bl_val = rd_int(g_blpath);
                if(bl_val > 0){
                    screen_wake_boost();
                } else if(bl_val == 0){
                    enforce_thermal_off();
                    maintain_charging();
                }
            }
        }
    }
    cleanup();
    if(ifd>=0){if(iwd>=0)inotify_rm_watch(ifd,iwd);close(ifd);}
    if(blfd>=0){if(blwd>=0)inotify_rm_watch(blfd,blwd);close(blfd);}
    if(xmfd>=0){close(xmfd);}

    if(intfd>=0){
        if(int_wd_mod>=0) inotify_rm_watch(intfd,int_wd_mod);
        if(int_wd_web>=0) inotify_rm_watch(intfd,int_wd_web);
        close(intfd);
    }
    return 0;
}
