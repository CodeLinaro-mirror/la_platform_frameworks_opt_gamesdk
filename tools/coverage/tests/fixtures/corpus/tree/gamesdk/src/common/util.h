#pragma once
// Shared header compiled into two modules through different path spellings.
static inline int util_twice(int value) {
    return value * 2;
}
int util_add(int a, int b);
