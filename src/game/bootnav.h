/* bootnav.h - straight to the main menu with the saved profile; see bootnav.c. */
#ifndef BO3_BOOTNAV_H
#define BO3_BOOTNAV_H

#include <stdint.h>

struct XBOX_GAMEPAD;

/* Before the title reads its saves: a seed profile from the APK into an
 * empty save folder (Android). */
void bo3_bootnav_seed(const char *save_dir);
/* The texture upscaler's shaders from the APK to the app's files (Android). */
void bo3_android_unpack_shaders(void);
/* Arms the navigator when a profile is saved. */
void bo3_bootnav_start(const char *save_dir);
/* Each flip (main thread). */
void bo3_bootnav_flip(void);
/* Each pad read: the press, on the menu player's pad. */
void bo3_bootnav_pad(uint32_t port, struct XBOX_GAMEPAD *g);

#endif
