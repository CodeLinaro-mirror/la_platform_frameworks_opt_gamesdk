#include "swappy/swappy_api.h"
#include "util.h"
int alpha_value(int seed) {
    int acc = 0;
    for (int i = 0; i < seed; ++i) {
        acc = util_add(acc, swappy_clamp(i, 2));
    }
    return acc;
}
