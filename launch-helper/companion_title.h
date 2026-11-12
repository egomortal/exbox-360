/* companion_title.h  -- benign title-ID launch + dashboard-target config for the
 * nxe dashboard companion daemon.  Added 2026-11-04.
 *
 * Scope: this file ONLY parses a config file and calls the documented PS5
 * launch system services (sceLncUtilLaunchApp / sceSystemServiceLaunchApp),
 * the same sequence the Ex-Box big-app's own launch_title() already uses.
 * It does not touch elevate_self(), the gpl/ loaders, or any kernel/credential
 * code -- those already exist in the daemon and are unchanged.
 *
 * Requires (already declared/linked in main.c): log_line(), last_launch,
 * return_from_game_armed, auto_attempted, and -lSceSystemService -lSceUserService.
 * #include this AFTER those globals and BEFORE launch_dashboard().
 */
#ifndef COMPANION_TITLE_H
#define COMPANION_TITLE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- system-service prototypes (no public header on target) --------------- */
struct lnc_app_param {
    uint32_t sz;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
};
int sceLncUtilLaunchApp(const char *title_id, const char *const *argv,
                        struct lnc_app_param *param);
int sceSystemServiceLaunchApp(const char *title_id, const char *const *argv,
                              void *param);
int sceUserServiceGetInitialUser(int32_t *user_id);
int sceUserServiceGetForegroundUser(int32_t *user_id);

/* --- configurable dashboard target --------------------------------------- */
/* Empty string => fall back to the legacy DASHBOARD_PATH / eboot.elf launch. */
static char companion_dashboard_title[16] = "";

static void companion_load_config(void) {
    FILE *f = fopen("/data/nxe/companion.conf", "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        char *nl = strpbrk(val, "\r\n");
        if (nl) *nl = '\0';
        if (!strcmp(key, "dashboard")) {
            snprintf(companion_dashboard_title, sizeof companion_dashboard_title,
                     "%s", val);
        }
    }
    fclose(f);
}

/* A PS5 title id is 4 uppercase letters + 5 digits (PPSA99735, NPXS40071,
 * FAKE93601, CUSAxxxxx ...).  Anything else is treated as a legacy path. */
static int companion_is_title_id(const char *s) {
    if (strlen(s) != 9) return 0;
    for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
    for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

/* Launch a registered title the way PS5 Upload / the big-app do: LncUtil with a
 * sized param, then with none, then SystemService.  Returns the service rc
 * (>=0 on success).  The daemon is already elevated, so these resolve here. */
static int companion_launch_title(const char *title_id) {
    /* Resolve the foreground user (proven practice); fall back to initial user. */
    int32_t uid = -1;
    if (sceUserServiceGetForegroundUser(&uid) != 0 || uid < 0)
        sceUserServiceGetInitialUser(&uid);
    const char *argv[] = { NULL };

    /* Order matches working PS5 homebrew launchers: SystemService first. */
    int rc = sceSystemServiceLaunchApp(title_id, argv, NULL);
    log_line("launch title=%s SystemService(fg uid=%d) rc=0x%x", title_id, (int)uid, (unsigned)rc);
    if (rc >= 0) goto launched;

    {
        struct lnc_app_param param;
        memset(&param, 0, sizeof param);
        param.sz = (uint32_t)sizeof param;
        param.user_id = (uint32_t)uid;
        rc = sceLncUtilLaunchApp(title_id, argv, &param);
        log_line("launch title=%s LncUtil(param) rc=0x%x", title_id, (unsigned)rc);
        if (rc >= 0) goto launched;
        rc = sceLncUtilLaunchApp(title_id, argv, NULL);
        log_line("launch title=%s LncUtil(null) rc=0x%x", title_id, (unsigned)rc);
    }

launched:
    if (rc >= 0) {
        last_launch = time(NULL);
        return_from_game_armed = 1;
        auto_attempted = 0; /* allow one automatic return after this handoff */
    }
    return rc;
}
#endif /* COMPANION_TITLE_H */
