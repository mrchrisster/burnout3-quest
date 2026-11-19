/* xrcam.h - the head in the game's camera (Quest port, experimental); see xrcam.c. */
#ifndef BO3_XRCAM_H
#define BO3_XRCAM_H

#include <stdint.h>

/* RwCameraBeginUpdate's camera (0 after it). */
void bo3_xrcam_begin(uint32_t cam);
/* At RwCameraBeginUpdate's entry: its camera from the guest stack. */
void bo3_xrcam_begin_call(void);
/* After _rwFrameSyncDirty, before the view is built: the camera turned. */
void bo3_xrcam_apply(void);
/* At the start of CB3Game::Update: the true camera back. */
void bo3_xrcam_restore(void);

#endif
