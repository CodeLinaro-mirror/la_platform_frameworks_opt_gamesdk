/*
 * Copyright 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <array>

namespace samples {

struct Circle {
    struct Color {
        Color(float r, float g, float b) : r(r), g(g), b(b) {}

        union {
            std::array<float, 3> values;
            struct {
                float r;
                float g;
                float b;
            };
        };
    };

    Circle(const Color& color, float radius, float x, float y)
          : color(color), radius(radius), x(x), y(y) {}

    static constexpr long MIN_SEGMENTS = 36;

    // Upper bound of the fragment shader workload loop. GLSL ES 1.00 only
    // guarantees highp int values in (-2^16, 2^16), so larger iteration counts
    // are not representable.
    static constexpr int kMaxGpuIterations = 65535;

    const Color color;
    const float radius;
    const float x;
    const float y;
};

} // namespace samples
