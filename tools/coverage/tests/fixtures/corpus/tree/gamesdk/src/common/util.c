#include "util.h"
int util_add(int a, int b) {
    if (a > 100) {
        return util_twice(a);
    }
    return a + b;
}
