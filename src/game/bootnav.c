/*
 * bootnav.c - straight to the main menu with the saved profile (Quest port).
 *
 * The title boots through its films, "Press START", Driver Details > LOAD
 * PROFILE and "Load successful > CONTINUE" before its main menu. When a
 * profile is saved, this presses the way through on the menu player's pad,
 * reading the front-end flow state each flip as the online navigator does
 * (onlinenav.c's nav_read, onlinescreens.c's flow offsets):
 *   0x508 films, 0x568 attract, 0x528 press START   Start
 *   0x4F0 memory unit check, 0x350 load profile     A (Profile 1 is the cursor's)
 *   0x0C0 the main menu                             done
 * It stops at the main menu, after BOOTNAV_GIVEUP_MS, or at the player's B.
 * With no profile saved it does nothing (the player makes one), unless the
 * APK carries a seed profile (assets/seed/UDATA/..., tools/android/build-apk.sh
 * BO3_SEED_SAVE): bo3_bootnav_seed copies it into the empty save folder
 * before the title reads it.
 * TAKEDAWN_AUTOLOAD=0 (env.txt) turns both off.
 */
#include "bootnav.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "platform/recomp_env.h"
#include "recomp_types.h"
#include "region.h"
#include "onlinescreens.h"
#include "onlinebot.h"
#include "xinput_xbox.h"

#if defined(__ANDROID__)
#include <SDL.h>
#endif

#define BN_FLOW  BO3_VA(0x00567170u, 0x00567490u, 0x00567530u)
#define BN_TITLE_ID "4541005b"
#define BOOTNAV_GIVEUP_MS 120000ull

enum { BN_OFF, BN_RUN, BN_DONE };
static int s_state = BN_OFF;
static unsigned long long s_t0, s_next, s_press_until;
static int s_btn;            /* 1 Start, 2 A */
static uint32_t s_last_flow = 0xFFFFFFFFu;
static int s_presses;

static unsigned long long bn_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

static int bn_wanted(void)
{
    const char *e = getenv("TAKEDAWN_AUTOLOAD");
    return !(e && e[0] == '0');
}

/* A profile is a folder under UDATA/<title> (the title's save game). */
static int bn_has_profile(const char *save_dir)
{
    char p[1024];
    DIR *d;
    struct dirent *e;
    int found = 0;
    snprintf(p, sizeof p, "%s/UDATA/%s", save_dir, BN_TITLE_ID);
    d = opendir(p);
    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL) {
        char q[1100];
        struct stat sb;
        if (e->d_name[0] == '.')
            continue;
        snprintf(q, sizeof q, "%s/%s", p, e->d_name);
        if (stat(q, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

#if defined(__ANDROID__)
/* One file of the APK's assets (SDL reads them by relative path) to dst. */
static int bn_copy_asset(const char *asset, const char *dst)
{
    SDL_RWops *in = SDL_RWFromFile(asset, "rb");
    FILE *out;
    char buf[16384];
    size_t n;
    if (!in)
        return 0;
    out = fopen(dst, "wb");
    if (!out) {
        SDL_RWclose(in);
        return 0;
    }
    while ((n = SDL_RWread(in, buf, 1, sizeof buf)) > 0)
        fwrite(buf, 1, n, out);
    SDL_RWclose(in);
    fclose(out);
    return 1;
}
#endif

void bo3_bootnav_seed(const char *save_dir)
{
#if defined(__ANDROID__)
    /* the seed's list, written by build-apk.sh: one relative path a line */
    SDL_RWops *lst;
    char list[4096], path[1024], dst[1300];
    size_t n;
    int copied = 0;
    char *line, *save;
    if (!save_dir || !bn_wanted() || bn_has_profile(save_dir))
        return;
    lst = SDL_RWFromFile("seed/list.txt", "rb");
    if (!lst)
        return;
    n = SDL_RWread(lst, list, 1, sizeof list - 1);
    SDL_RWclose(lst);
    list[n] = 0;
    for (line = strtok_r(list, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s;
        if (!*line)
            continue;
        snprintf(path, sizeof path, "seed/%s", line);
        snprintf(dst, sizeof dst, "%s/%s", save_dir, line);
        for (s = dst + strlen(save_dir) + 1; *s; s++)       /* the folders on the way */
            if (*s == '/') {
                *s = 0;
                mkdir(dst, 0770);
                *s = '/';
            }
        copied += bn_copy_asset(path, dst);
    }
    fprintf(stderr, "[BOOTNAV] no profile saved: %d files of the seed profile copied to %s\n",
            copied, save_dir);
#else
    (void)save_dir;
#endif
}

/* The texture upscaler's shader files (graphics.texture_upscale), packed in
 * the APK as assets/upscale_shaders (build-apk.sh), unpacked once a version
 * to <app files>/shaders/upscale, where nv2a_pb_vulkan.c's texup_init_once
 * looks (recomp_exe_dir() is the app's files folder on Android). */
void bo3_android_unpack_shaders(void)
{
#if defined(__ANDROID__)
    const char *dir = getenv("RECOMP_ANDROID_DIR");
    SDL_RWops *in;
    char want[64] = "", have[64] = "", p[1024], dst[1100], list[2048], *line, *save;
    size_t n;
    FILE *f;
    int copied = 0;
    if (!dir || !*dir)
        return;
    if (!(in = SDL_RWFromFile("upscale_shaders/VERSION", "rb")))
        return;
    n = SDL_RWread(in, want, 1, sizeof want - 1);
    SDL_RWclose(in);
    want[n] = 0;
    snprintf(p, sizeof p, "%s/shaders/upscale/VERSION", dir);
    if ((f = fopen(p, "rb")) != NULL) {
        n = fread(have, 1, sizeof have - 1, f);
        fclose(f);
        have[n] = 0;
        if (!strcmp(have, want))
            return;          /* already there */
    }
    if (!(in = SDL_RWFromFile("upscale_shaders/list.txt", "rb")))
        return;
    n = SDL_RWread(in, list, 1, sizeof list - 1);
    SDL_RWclose(in);
    list[n] = 0;
    snprintf(p, sizeof p, "%s/shaders", dir);
    mkdir(p, 0770);
    snprintf(p, sizeof p, "%s/shaders/upscale", dir);
    mkdir(p, 0770);
    for (line = strtok_r(list, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char a[256];
        if (!*line || !strcmp(line, "VERSION"))
            continue;
        snprintf(a, sizeof a, "upscale_shaders/%s", line);
        snprintf(dst, sizeof dst, "%s/%s", p, line);
        copied += bn_copy_asset(a, dst);
    }
    snprintf(dst, sizeof dst, "%s/VERSION", p);   /* last: a stopped copy is redone */
    copied += bn_copy_asset("upscale_shaders/VERSION", dst);
    fprintf(stderr, "[TEXUP] %d shader files unpacked to %s\n", copied, p);
#endif
}

void bo3_bootnav_start(const char *save_dir)
{
    if (!bn_wanted()) {
        fprintf(stderr, "[BOOTNAV] off (TAKEDAWN_AUTOLOAD=0)\n");
        return;
    }
    if (!save_dir || !bn_has_profile(save_dir)) {
        fprintf(stderr, "[BOOTNAV] no profile saved: the title's own screens\n");
        return;
    }
    s_state = BN_RUN;
    s_t0 = bn_ms();
    fprintf(stderr, "[BOOTNAV] on: straight to the main menu with the saved profile\n");
}

static void bn_stop(const char *why)
{
    s_state = BN_DONE;
    s_press_until = 0;
    fprintf(stderr, "[BOOTNAV] done: %s (%d presses, %.1f s)\n", why, s_presses,
            (double)(bn_ms() - s_t0) / 1000.0);
}

void bo3_bootnav_flip(void)
{
    uint32_t cur, flow;
    unsigned long long now;
    int btn = 0;
    if (s_state != BN_RUN)
        return;
    now = bn_ms();
    if (now - s_t0 > BOOTNAV_GIVEUP_MS) {
        bn_stop("gave up (the main menu not reached)");
        return;
    }
    cur = MEM32(BN_FLOW + 4);
    flow = (cur >= 0x10000u && cur < 0x04000000u) ? cur - BN_FLOW : 0;
    if (flow != s_last_flow) {
        fprintf(stderr, "[BOOTNAV] flow 0x%03X\n", flow);
        s_last_flow = flow;
        s_next = now + 700;      /* let a new page settle before a press */
    }
    switch (flow) {
    case 0x0C0:
        bn_stop("main menu");
        return;
    case 0x508: case 0x568: case 0x528:
        btn = 1;
        break;
    case 0x4F0: case 0x350:
        btn = 2;
        break;
    default:
        return;              /* loading, between pages: wait */
    }
    if (now >= s_next && now >= s_press_until) {
        s_btn = btn;
        s_press_until = now + 150;
        s_next = now + 1000;
        s_presses++;
    }
}

void bo3_bootnav_pad(uint32_t port, struct XBOX_GAMEPAD *g)
{
    if (s_state != BN_RUN || !g)
        return;
    if (g->bAnalogButtons[XBOX_BUTTON_B] >= XBOX_ANALOG_BUTTON_THRESHOLD) {
        bn_stop("B: the player took over");
        return;
    }
    if (port != 2u || bn_ms() >= s_press_until)
        return;
    if (s_btn == 1)
        g->wButtons |= XBOX_GAMEPAD_START;
    else if (s_btn == 2)
        g->bAnalogButtons[XBOX_BUTTON_A] = 255;
}
