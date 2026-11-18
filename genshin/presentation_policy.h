#pragma once
#include <cstdlib>
#include <cstring>
#include <strings.h>

// The game owns its CAMetalLayer pacing. Reducing a three-deep drawable queue
// to two saves about one frame of latency but can halve the frame rate, so it
// stays an explicit diagnostic opt-in rather than a default side effect.
static bool ReducedDrawableQueueRequested(const char *value) {
    if(!value || !*value) return false;
    return !strcmp(value,"1") || !strcasecmp(value,"true") || !strcasecmp(value,"yes");
}
static bool ReducedDrawableQueueRequested(void) {
    return ReducedDrawableQueueRequested(getenv("GDM_TWO_DRAWABLES"));
}
