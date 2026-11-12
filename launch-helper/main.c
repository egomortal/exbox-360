// NXE Dashboard companion payload: local IPC, homebrew scanning and relaunch.
// Copyright (C) 2026 NXE PS5 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include "gpl/hbldr.h"

#define TITLE_ID "FAKE93601"
#define DASHBOARD_PATH "/data/homebrew/ExBox360/eboot.elf"
#define DASHBOARD_DIR "/data/homebrew/ExBox360"
#define PORT 7090
#define MAX_APPS 128
#define LOG_PATH "/data/nxe/dashboard-daemon.log"

typedef struct { char title[128]; char path[1024]; } App;
int sceUserServiceInitialize(void *);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int, char *);

static volatile sig_atomic_t stopping;
static volatile sig_atomic_t launch_requested;
static time_t handoff_until;
static time_t last_launch;
static int suspended;
static int return_from_game_armed;
// One automatic launch per daemon lifetime. A process ID is not proof that
// the dashboard reached the foreground, so repeated starts can freeze Shell.
static int auto_attempted;
static int auto_launch_pending;
static App apps[MAX_APPS];
static int app_count;
static const char *roots[] = {
    "/data/homebrew", "/mnt/usb0/homebrew", "/mnt/usb1/homebrew",
    "/mnt/ext0/homebrew", "/mnt/ext1/homebrew"
};

static void on_signal(int sig) { (void)sig; stopping = 1; }

static void log_line(const char *fmt, ...) {
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    fprintf(f, "[%ld] ", (long)time(NULL));
    va_list args; va_start(args, fmt); vfprintf(f, fmt, args); va_end(args);
    fputc('\n', f);
    fclose(f);
}

static int elevate_self(void) {
    static const uint8_t caps[16] = {
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
    };
    return kernel_set_ucred_authid(-1, 0x4801000000000013L) ||
           kernel_set_ucred_uid(-1, 0) || kernel_set_ucred_caps(-1, caps);
}

static int safe_name(const char *s) {
    if (!s || !*s || !strcmp(s, ".") || !strcmp(s, "..") || strlen(s) >= 120)
        return 0;
    for (; *s; s++) if (*s == '/' || *s == '\\' || *s == '\n' || *s == '\r' || *s == '\t') return 0;
    return 1;
}

static void scan_apps(void) {
    app_count = 0;
    for (size_t r = 0; r < sizeof roots / sizeof roots[0]; r++) {
        DIR *dir = opendir(roots[r]);
        if (!dir) continue;
        struct dirent *ent;
        while ((ent = readdir(dir)) && app_count < MAX_APPS) {
            if (!safe_name(ent->d_name) || !strcmp(ent->d_name, TITLE_ID) ||
                !strcmp(ent->d_name, "ExBox360")) continue;
            char folder[1024], eboot[1024];
            if (snprintf(folder, sizeof folder, "%s/%s", roots[r], ent->d_name) >= (int)sizeof folder ||
                snprintf(eboot, sizeof eboot, "%s/eboot.elf", folder) >= (int)sizeof eboot) continue;
            struct stat st;
            if (stat(folder, &st) || !S_ISDIR(st.st_mode) || stat(eboot, &st) || !S_ISREG(st.st_mode)) continue;
            App *a = &apps[app_count++];
            snprintf(a->title, sizeof a->title, "%s", ent->d_name);
            snprintf(a->path, sizeof a->path, "%s", eboot);
        }
        closedir(dir);
    }
}

static int write_all(int fd, const char *s, size_t n) {
    while (n) {
        ssize_t written = send(fd, s, n, 0);
        if (written <= 0) return -1;
        s += written; n -= (size_t)written;
    }
    return 0;
}

static void reply(int fd, const char *s) { (void)write_all(fd, s, strlen(s)); }

#include "companion_title.h"

static int launch_dashboard(void) {
    if (companion_dashboard_title[0]) {
        int trc = companion_launch_title(companion_dashboard_title);
        if (trc >= 0) return 0;
        log_line("dashboard title launch failed rc=0x%x; using path", (unsigned)trc);
    }
    char *argv[] = {DASHBOARD_PATH, NULL};
    char *envp[] = {NULL};
    pid_t pid = hbldr_launch(DASHBOARD_DIR, DASHBOARD_PATH, -1, argv, envp);
    last_launch = time(NULL);
    log_line("launch %s pid=%d", DASHBOARD_PATH, (int)pid);
    if (pid >= 0) return_from_game_armed = 0;
    return pid < 0 ? -1 : 0;
}

static int foreground_kind(void) {
    int mib[4] = {1, 14, 8, 0};
    size_t size = 0;
    if (!sysctl(mib, 4, NULL, &size, NULL, 0) && size > 0 && size < 4 * 1024 * 1024) {
        unsigned char *records = malloc(size);
        if (records) {
            if (!sysctl(mib, 4, records, &size, NULL, 0)) {
                size_t offset = 0;
                while (offset + sizeof(int) <= size) {
                    int length = 0;
                    memcpy(&length, records + offset, sizeof length);
                    if (length < (int)(offsetof(struct kinfo_proc, ki_comm) + 20) ||
                        offset + (size_t)length > size) break;
                    char name[20];
                    memcpy(name, records + offset + offsetof(struct kinfo_proc, ki_comm), sizeof name);
                    if (memchr(name, 0, sizeof name) && !strcmp(name, "eboot.elf")) {
                        free(records);
                        return 2;
                    }
                    offset += (size_t)length;
                }
            }
            free(records);
        }
    }
    int id = sceSystemServiceGetAppIdOfRunningBigApp();
    if (id < 0) return 0;
    char title[64] = {0};
    if (sceSystemServiceGetAppTitleId(id, title)) return 0;
    if (!strcmp(title, TITLE_ID)) return 1;
    if (!strncmp(title, "NPXS", 4)) return 0;
    return 2;
}

static void launch_homebrew(const char *path) {
    char cwd[1024];
    snprintf(cwd, sizeof cwd, "%s", path);
    char *slash = strrchr(cwd, '/');
    if (!slash) return;
    *slash = 0;
    char *argv[] = {(char *)path, NULL};
    char *envp[] = {NULL};
    handoff_until = time(NULL) + 30;
    pid_t pid = hbldr_launch(cwd, path, -1, argv, envp);
    handoff_until = pid < 0 ? time(NULL) + 5 : 0;
    if (pid >= 0) {
        return_from_game_armed = 1;
        auto_attempted = 0; // one automatic return after this intentional handoff
    }
    log_line("homebrew launch path=%s pid=%d", path, (int)pid);
}

static void handle_client(int fd) {
    const struct timeval timeout = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    char command[1200]; size_t used = 0;
    while (used < sizeof command - 1) {
        ssize_t got = recv(fd, command + used, sizeof command - 1 - used, 0);
        if (got <= 0) break;
        used += (size_t)got;
        if (memchr(command, '\n', used)) break;
    }
    command[used] = 0;
    char *end = strchr(command, '\n');
    if (!end) { reply(fd, "ERR\n"); return; }
    *end = 0;
    if (!strncmp(command, "GET /launch HTTP/1.", 18)) {
        // The PS5 Home tile uses a loopback deeplink. Reply before launching so
        // Shell never waits on a big-app transition or opens a browser page.
        reply(fd, "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        suspended = 0;
        return_from_game_armed = 0;
        launch_requested = 1;
    } else if (!strcmp(command, "LIST")) {
        suspended = 0;
        auto_launch_pending = 0;
        scan_apps();
        reply(fd, "OK\n");
        for (int i = 0; i < app_count; i++) {
            char line[1200];
            int n = snprintf(line, sizeof line, "%s\t%s\n", apps[i].title, apps[i].path);
            if (n > 0 && n < (int)sizeof line) (void)write_all(fd, line, (size_t)n);
        }
        reply(fd, ".\n");
    } else if (!strcmp(command, "SUSPEND")) {
        suspended = 1;
        auto_launch_pending = 0;
        return_from_game_armed = 0;
        reply(fd, "OK\n");
    } else if (!strncmp(command, "LAUNCH\t", 7)) {
        const char *arg = command + 7;
        if (companion_is_title_id(arg)) {
            int rc = companion_launch_title(arg);
            if (rc >= 0) { suspended = 0; reply(fd, "OK\n"); }
            else { char msg[48]; snprintf(msg, sizeof msg, "ERR %x\n", (unsigned)rc); reply(fd, msg); }
            return;
        }
        const char *path = arg;
        scan_apps();
        int found = 0;
        for (int i = 0; i < app_count; i++) if (!strcmp(path, apps[i].path)) found = 1;
        if (!found) { reply(fd, "ERR\n"); return; }
        reply(fd, "OK\n");
        shutdown(fd, SHUT_RDWR);
        launch_homebrew(path);
    } else {
        reply(fd, "ERR\n");
    }
}

int main(void) {
    mkdir("/data/nxe", 0755);
    log_line("daemon starting");
    if (sceUserServiceInitialize(NULL) || elevate_self()) { log_line("initialization failed"); return 1; }
    companion_load_config();
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { log_line("socket failed errno=%d", errno); return 1; }
    int one = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, (struct sockaddr *)&address, sizeof address) || listen(server, 4)) {
        log_line("bind/listen failed errno=%d (another daemon may be running)", errno);
        close(server); return 1;
    }
    log_line("loopback service ready port=%d", PORT);
    int previous = foreground_kind();
    time_t home_since = 0;
    time_t started = time(NULL);
    while (!stopping) {
        fd_set ready; FD_ZERO(&ready); FD_SET(server, &ready);
        struct timeval wait = {0, 500000};
        int selected = select(server + 1, &ready, NULL, NULL, &wait);
        if (selected > 0 && FD_ISSET(server, &ready)) {
            int client = accept(server, NULL, NULL);
            if (client >= 0) { handle_client(client); close(client); }
        }
        if (launch_requested) {
            launch_requested = 0;
            auto_launch_pending = 0;
            if (foreground_kind() != 2) (void)launch_dashboard();
        }
        time_t now = time(NULL);
        int current = foreground_kind();
        if (current == 2) auto_launch_pending = 0;
        if (auto_launch_pending && now >= last_launch + 15) {
            auto_launch_pending = 0;
            suspended = 1;
            log_line("automatic launch never reached foreground; retries paused");
        }
        if (current == 1) suspended = 0;
        if (current == 0 && previous == 2 && !return_from_game_armed) {
            suspended = 1;
            log_line("system Home selected; automatic relaunch paused");
        }
        if (current == 0 && previous != 0) home_since = now;
        if (current != 0) home_since = 0;
        if (current == 0 && !home_since && now >= started + 6 && !auto_attempted)
            home_since = now;
        if (current == 0 && home_since && now >= home_since + 3 &&
            now >= handoff_until && !suspended && !auto_attempted && now >= last_launch + 10) {
            auto_attempted = 1;
            if (launch_dashboard() < 0) {
                suspended = 1;
                log_line("automatic launch failed; retries paused");
            } else auto_launch_pending = 1;
            home_since = 0;
        }
        previous = current;
    }
    close(server);
    log_line("daemon stopped");
    return 0;
}
