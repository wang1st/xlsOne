#ifndef XLSONE_PLATFORM_DROP_H
#define XLSONE_PLATFORM_DROP_H

#include <SDL.h>

typedef struct xls_platform_drop_target {
    void *implementation;
} xls_platform_drop_target;

/*
 * Adds pre-release drag-enter/leave events where SDL 2 does not expose them.
 * Actual files continue to arrive through SDL_DROPFILE.
 */
int xls_platform_drop_target_init(
    xls_platform_drop_target *target,
    SDL_Window *window
);

void xls_platform_drop_target_shutdown(
    xls_platform_drop_target *target
);

#endif
