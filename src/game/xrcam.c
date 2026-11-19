/*
 * xrcam.c - the head in the game's camera (Quest port, experimental).
 *
 * The view being drawn (the race's view, VIEW_PTR, hirefresh.c's blend_view)
 * holds its RwCamera at + 0x58; the camera's frame is the RwObject's parent
 * (+ 4) and its LTM (+ 0x50: right, flags, up, at, pos; RW 3.7 RwMatrix) is
 * what RwCameraBeginUpdate builds the device's view from, right after
 * _rwFrameSyncDirty (sub_001E15E0, wrapped in hirefresh.c). After that sync
 * the camera's LTM is turned here by the head's rotation (in the camera's own
 * axes) and moved by its offset; the true LTM is put back at the start of the
 * next CB3Game::Update (bo3_update), before any step, so the simulation never
 * reads the turned one (as the high-refresh blends are put back).
 *
 * Stage 1 (this file now): a fixed test turn (TAKEDAWN_XRCAM_YAW=<degrees>,
 * TAKEDAWN_XRCAM_PITCH) proves the hook, and the camera's view window and
 * clip planes are logged (TAKEDAWN_XRCAM_LOG=1) to check RwCamera's layout:
 * viewWindow at + 0x68 (tan of the half angles), nearPlane + 0x80, farPlane
 * + 0x84. Stage 2: the headset's pose from xr_theater.c each picture, the eye
 * field of view in the view window, and the frame shown as a projection layer.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "region.h"
#include "recomp_types.h"
#include "xrcam.h"

#define VIEW_PTR     BO3_VA(0x4D6520u, 0x4D6840u, 0x4D68E0u)
#define VIEW_CAMERA  0x58u
#define FRAME_LTM    0x50u
#define CAM_VIEWWIN  0x68u
#define CAM_NEAR     0x80u
#define CAM_FAR      0x84u

static int      s_on = -1;
static float    s_yaw, s_pitch;   /* radians, test turn */
static int      s_log;
static uint32_t s_frame;          /* the LTM turned in this picture */
static float    s_true[16];
static unsigned s_applied, s_logged;

static int ok_va(uint32_t va) { return va >= 0x10000u && va < 0x08000000u; }

static void init(void)
{
    const char *y = getenv("TAKEDAWN_XRCAM_YAW"), *p = getenv("TAKEDAWN_XRCAM_PITCH");
    const char *l = getenv("TAKEDAWN_XRCAM_LOG");
    s_yaw = y ? (float)atof(y) * 3.14159265f / 180.0f : 0.0f;
    s_pitch = p ? (float)atof(p) * 3.14159265f / 180.0f : 0.0f;
    s_log = l && l[0] == '1';
    s_on = (s_yaw != 0.0f || s_pitch != 0.0f || s_log);
    if (s_on)
        fprintf(stderr, "[XRCAM] on: test turn yaw %.1f pitch %.1f degrees%s\n",
                s_yaw * 180.0f / 3.14159265f, s_pitch * 180.0f / 3.14159265f,
                s_log ? ", camera log" : "");
}

/* v' = a * cos + b * sin, for 3-vectors */
static void turn(float *a, float *b, float ang)
{
    float c = cosf(ang), s = sinf(ang), na[3], nb[3];
    int i;
    for (i = 0; i < 3; i++) {
        na[i] = a[i] * c + b[i] * s;
        nb[i] = b[i] * c - a[i] * s;
    }
    memcpy(a, na, sizeof na);
    memcpy(b, nb, sizeof nb);
}

static uint32_t s_begin_cam;      /* the camera RwCameraBeginUpdate is building */

void bo3_xrcam_begin(uint32_t cam) { s_begin_cam = cam; }
/* At RwCameraBeginUpdate's entry: its argument, the camera (cdecl). */
void bo3_xrcam_begin_call(void) { s_begin_cam = MEM32(g_esp + 4); }

void bo3_xrcam_apply(void)
{
    uint32_t v, cam, frame;
    float *m;

    if (s_on < 0)
        init();
    if (!s_on || s_frame || !s_begin_cam)
        return;               /* once a picture, inside a camera's BeginUpdate */
    v = MEM32(VIEW_PTR);
    if (!ok_va(v))
        return;
    cam = MEM32(v + VIEW_CAMERA);
    if (!ok_va(cam) || cam != s_begin_cam)
        return;               /* the view's own camera, not a reflection's or a shadow's */
    frame = MEM32(cam + 4u);
    if (!ok_va(frame))
        return;
    m = (float *)(uintptr_t)XBOX_PTR(frame + FRAME_LTM);
    if (s_log && (s_logged++ % 300u) == 0) {
        const float *vw = (const float *)(uintptr_t)XBOX_PTR(cam + CAM_VIEWWIN);
        fprintf(stderr, "[XRCAM] camera %08x frame %08x: view window %.4f %.4f (%.1f x %.1f deg),"
                " near %.3f far %.1f; at %.3f %.3f %.3f pos %.1f %.1f %.1f\n", cam, frame, vw[0], vw[1],
                2.0f * atanf(vw[0]) * 180.0f / 3.14159265f, 2.0f * atanf(vw[1]) * 180.0f / 3.14159265f,
                *(const float *)(uintptr_t)XBOX_PTR(cam + CAM_NEAR),
                *(const float *)(uintptr_t)XBOX_PTR(cam + CAM_FAR), m[8], m[9], m[10], m[12], m[13], m[14]);
    }
    if (s_yaw == 0.0f && s_pitch == 0.0f)
        return;
    memcpy(s_true, m, sizeof s_true);
    s_frame = frame;
    /* RwMatrix rows: right (0..2), up (4..6), at (8..10), pos (12..14) */
    if (s_yaw != 0.0f)
        turn(&m[0], &m[8], s_yaw);      /* about up: right and at */
    if (s_pitch != 0.0f)
        turn(&m[8], &m[4], s_pitch);    /* about right: at and up */
    s_applied++;
}

void bo3_xrcam_restore(void)
{
    if (!s_frame)
        return;
    memcpy((float *)(uintptr_t)XBOX_PTR(s_frame + FRAME_LTM), s_true, sizeof s_true);
    s_frame = 0;
}
