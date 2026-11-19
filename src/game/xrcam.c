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

/* the toolkit: the headset's pose (video/xr_theater.h), the push buffer's marks */
int xrt_immersive(void);
uint32_t xrt_pose_take(float q[4], float p[3], float *tan_half_h);
void xrt_immersive_opts(int *level, float *aspect);
void xrt_pose_set_tanv(uint32_t id, float tanv);
void nv2a_pb_stream_mark(uint32_t va, uint32_t tag);
#define D3D_DEVICE   BO3_VA(0x35FB48, 0x35FE48, 0x35FEE8)   /* main.c hdr_mark: m_pPut at + 0 */
#define XRCAM_SCALE  1.0f     /* world units a metre (the head's movement) */

#define VIEW_PTR     BO3_VA(0x4D6520u, 0x4D6840u, 0x4D68E0u)
#define VIEW_CAMERA  0x58u
#define FRAME_LTM    0x50u
#define CAM_VIEWWIN  0x68u
#define CAM_NEAR     0x80u
#define CAM_FAR      0x84u
#define DIRTY_HEAD   BO3_VA(0x75935Cu, 0x75967Cu, 0x75971Cu)     /* RenderWare's dirty frame list */
#define FRAME_MODEL  0x10u
#define FRAME_ROOT   0xA0u

static int      s_on = -1;
static float    s_yaw, s_pitch;   /* radians, test turn */
static int      s_log;
static uint32_t s_frame;          /* the LTM turned in this picture */
static float    s_true[16];
static uint32_t s_cam;            /* its camera: the view window turned too */
static float    s_true_vw[2];
static unsigned s_imm_said;
static int      s_pre;            /* turned before the sync (the modelling matrix) */
static float    s_turned[16];     /* what the sync made of it */
static float    s_true_model[16];
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

static void norm3(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static void cross3(const float *a, const float *b, float *o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

/* The view's camera, before RwCameraBeginUpdate's sync: its modelling
 * matrix turned and moved by the head (a root frame, dirty every picture in
 * a race), and the view window widened to the headset's, so the sync builds
 * the LTM and the camera's culling frustum from them. */
static void immersive_turn(uint32_t cam)
{
    uint32_t v = MEM32(VIEW_PTR), frame, id, dev, put;
    float q[4], p[3], th, R[9], M[9], ax[3][3], nx[3][3], aspect;
    float *mo, *vw;
    int i, j, k, level;
    static const float S[3] = {-1.0f, 1.0f, -1.0f};   /* OpenXR's head axes to the camera's:
                                                        * RW's right points to the screen's left */
    if (!ok_va(v) || MEM32(v + VIEW_CAMERA) != cam)
        return;
    frame = MEM32(cam + 4u);
    if (!ok_va(frame) || MEM32(frame + FRAME_ROOT) != frame)
        return;
    vw = (float *)(uintptr_t)XBOX_PTR(cam + CAM_VIEWWIN);
    if (vw[0] <= 0.0f || vw[1] <= 0.0f)
        return;
    id = xrt_pose_take(q, p, &th);
    if (!id || th <= 0.0f)
        return;
    xrt_immersive_opts(&level, &aspect);
    mo = (float *)(uintptr_t)XBOX_PTR(frame + FRAME_MODEL);
    memcpy(s_true_model, mo, sizeof s_true_model);
    memcpy(s_true, (float *)(uintptr_t)XBOX_PTR(frame + FRAME_LTM), sizeof s_true);
    memcpy(s_true_vw, vw, sizeof s_true_vw);
    for (i = 0; i < 3; i++)                 /* right, up, at */
        for (k = 0; k < 3; k++)
            ax[i][k] = mo[i * 4 + k];
    if (level) {
        /* level horizon: the camera's heading only (world up is +y) */
        float up[3] = {0.0f, 1.0f, 0.0f}, at[3] = {ax[2][0], 0.0f, ax[2][2]}, rc[3], cu[3];
        norm3(at);
        cross3(ax[1], ax[2], cu);           /* the handedness the camera's axes have */
        cross3(up, at, rc);
        if (cu[0] * ax[0][0] + cu[1] * ax[0][1] + cu[2] * ax[0][2] < 0.0f)
            rc[0] = -rc[0], rc[1] = -rc[1], rc[2] = -rc[2];
        memcpy(ax[0], rc, sizeof rc);
        memcpy(ax[1], up, sizeof up);
        memcpy(ax[2], at, sizeof at);
    }
    R[0] = 1 - 2 * (q[1] * q[1] + q[2] * q[2]); R[1] = 2 * (q[0] * q[1] - q[2] * q[3]); R[2] = 2 * (q[0] * q[2] + q[1] * q[3]);
    R[3] = 2 * (q[0] * q[1] + q[2] * q[3]); R[4] = 1 - 2 * (q[0] * q[0] + q[2] * q[2]); R[5] = 2 * (q[1] * q[2] - q[0] * q[3]);
    R[6] = 2 * (q[0] * q[2] - q[1] * q[3]); R[7] = 2 * (q[1] * q[2] + q[0] * q[3]); R[8] = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            M[i * 3 + j] = S[i] * R[i * 3 + j] * S[j];
    for (j = 0; j < 3; j++)
        for (k = 0; k < 3; k++)
            nx[j][k] = M[0 * 3 + j] * ax[0][k] + M[1 * 3 + j] * ax[1][k] + M[2 * 3 + j] * ax[2][k];
    for (j = 0; j < 3; j++)
        for (k = 0; k < 3; k++)
            mo[j * 4 + k] = nx[j][k];
    for (k = 0; k < 3; k++)
        mo[12 + k] = s_true_model[12 + k]
                   + XRCAM_SCALE * (S[0] * p[0] * ax[0][k] + S[1] * p[1] * ax[1][k] + S[2] * p[2] * ax[2][k]);
    memcpy(s_turned, mo, sizeof s_turned);  /* a root frame: its LTM is its modelling matrix */
    /* the field of view: the headset's width; the height the game's (0), 4:3 or the width */
    vw[0] = th;
    vw[1] = aspect > 0.0f ? th * aspect : th * s_true_vw[1] / s_true_vw[0];
    xrt_pose_set_tanv(id, vw[1]);
    s_frame = frame;
    s_cam = cam;
    s_pre = 1;
    dev = MEM32(D3D_DEVICE);                /* the pose's index where this picture's commands start */
    put = dev ? MEM32(dev) : 0;
    if (put)
        nv2a_pb_stream_mark(put, 0x80000000u | id);
    if (!s_imm_said++)
        fprintf(stderr, "[XRCAM] head-tracked: the camera follows the head before its sync (view window"
                " %.3f x %.3f, was %.3f x %.3f; level horizon %d)\n", vw[0], vw[1], s_true_vw[0], s_true_vw[1], level);
    s_applied++;
}

/* At RwCameraBeginUpdate's entry: its argument, the camera (cdecl). */
void bo3_xrcam_begin_call(void)
{
    static unsigned n;
    uint32_t v, frame, l;
    int dirty = 0, k = 0;
    s_begin_cam = MEM32(g_esp + 4);
    if (s_on < 0)
        init();
    if (!s_frame && xrt_immersive())
        immersive_turn(s_begin_cam);
    if (!s_log)
        return;
    v = MEM32(VIEW_PTR);
    if (!ok_va(v) || MEM32(v + VIEW_CAMERA) != s_begin_cam || (n++ % 300u))
        return;
    frame = MEM32(s_begin_cam + 4u);
    if (!ok_va(frame))
        return;
    for (l = MEM32(DIRTY_HEAD); l != DIRTY_HEAD && l && k < 4096; l = MEM32(l), k++)
        if (l - 8u == frame)
            dirty = 1;
    {
        const float *mo = (const float *)(uintptr_t)XBOX_PTR(frame + FRAME_MODEL);
        const float *lt = (const float *)(uintptr_t)XBOX_PTR(frame + FRAME_LTM);
        fprintf(stderr, "[XRCAM] begin: frame %08x root %s, %s the dirty list (flags %02x), model at %.3f %.3f %.3f"
                " ltm at %.3f %.3f %.3f\n", frame, MEM32(frame + FRAME_ROOT) == frame ? "itself" : "another",
                dirty ? "in" : "not in", (unsigned)(MEM32(MEM32(frame + FRAME_ROOT)) >> 24),
                mo[8], mo[9], mo[10], lt[8], lt[9], lt[10]);
    }
}

void bo3_xrcam_apply(void)
{
    uint32_t v, cam, frame;
    float *m;

    if (s_on < 0)
        init();
    if (s_pre && s_begin_cam) {
        uint32_t v2 = MEM32(VIEW_PTR), c2 = ok_va(v2) ? MEM32(v2 + VIEW_CAMERA) : 0;
        if (c2 == s_begin_cam && ok_va(MEM32(c2 + 4u)) && MEM32(c2 + 4u) == s_frame)
            ;                 /* the turned camera's own sync: below */
        else
            return;
    } else if (s_frame || !s_begin_cam || !s_on) {
        return;               /* once a picture, inside a camera's BeginUpdate */
    }
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
    if (s_pre) {
        /* turned before the sync: a frame that was not dirty kept its old
         * LTM, so the turned matrix goes there now (its culling then lags) */
        if (memcmp(m, s_turned, sizeof s_turned))
            memcpy(m, s_turned, sizeof s_turned);
        return;
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
    if (s_pre) {
        /* the modelling matrix back, and the LTM as it was (the next sync
         * makes it again from the modelling matrix) */
        memcpy((float *)(uintptr_t)XBOX_PTR(s_frame + FRAME_MODEL), s_true_model, sizeof s_true_model);
        memcpy((float *)(uintptr_t)XBOX_PTR(s_frame + FRAME_LTM), s_true_model, sizeof s_true_model);
        s_pre = 0;
    } else {
        memcpy((float *)(uintptr_t)XBOX_PTR(s_frame + FRAME_LTM), s_true, sizeof s_true);
    }
    if (s_cam) {
        memcpy((float *)(uintptr_t)XBOX_PTR(s_cam + CAM_VIEWWIN), s_true_vw, sizeof s_true_vw);
        s_cam = 0;
    }
    s_frame = 0;
}
