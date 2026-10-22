#pragma once
// Public header reached through the games-frame-pacing/include/swappy symlink.
static inline int swappy_clamp(int value, int limit) {
    if (value > limit) {
        return limit;
    }
    return value;
}
