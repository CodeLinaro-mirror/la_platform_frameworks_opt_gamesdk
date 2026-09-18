/*
 * Copyright 2026 The Android Open Source Project
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

#include "VulkanCircleRenderer.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

#include "OverlayBatcher.h"
#include "VulkanOverlayRenderer.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"

namespace samples::vulkan {

namespace {

struct CircleVertex {
    float x;
    float y;
};

struct CirclePushConstants {
    float uAspectRatio;
    float uOffsetX;
    float uOffsetY;
    float uRadius;
    float uColorR;
    float uColorG;
    float uColorB;
    int32_t uGpuWorkload;
};
static_assert(sizeof(CirclePushConstants) == 32, "Unexpected push constant size");

/*
 * Compiled with NDK glslc (-O -mfmt=num) from GLSL 450:
 *
 * // circle.vert
 * #version 450
 * layout(location = 0) in vec2 aPosition;
 * layout(push_constant) uniform PushConstants {
 *     float uAspectRatio;
 *     float uOffsetX;
 *     float uOffsetY;
 *     float uRadius;
 *     float uColorR;
 *     float uColorG;
 *     float uColorB;
 *     int uGpuWorkload;
 * } pc;
 * layout(location = 0) out vec2 vLocalPos;
 * void main() {
 *     vLocalPos = aPosition;
 *     gl_Position = vec4(
 *         aPosition.x * pc.uRadius / pc.uAspectRatio + pc.uOffsetX,
 *         aPosition.y * pc.uRadius + pc.uOffsetY,
 *         0.0, 1.0);
 * }
 */
constexpr uint32_t kCircleVertSpv[] = {
        0x07230203, 0x00010000, 0x000d000a, 0x00000037, 0x00000000, 0x00020011, 0x00000001,
        0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
        0x00000000, 0x00000001, 0x0008000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000,
        0x00000009, 0x0000000b, 0x00000013, 0x00040047, 0x00000009, 0x0000001e, 0x00000000,
        0x00040047, 0x0000000b, 0x0000001e, 0x00000000, 0x00050048, 0x00000011, 0x00000000,
        0x0000000b, 0x00000000, 0x00050048, 0x00000011, 0x00000001, 0x0000000b, 0x00000001,
        0x00050048, 0x00000011, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x00000011,
        0x00000003, 0x0000000b, 0x00000004, 0x00030047, 0x00000011, 0x00000002, 0x00050048,
        0x0000001a, 0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000001a, 0x00000001,
        0x00000023, 0x00000004, 0x00050048, 0x0000001a, 0x00000002, 0x00000023, 0x00000008,
        0x00050048, 0x0000001a, 0x00000003, 0x00000023, 0x0000000c, 0x00050048, 0x0000001a,
        0x00000004, 0x00000023, 0x00000010, 0x00050048, 0x0000001a, 0x00000005, 0x00000023,
        0x00000014, 0x00050048, 0x0000001a, 0x00000006, 0x00000023, 0x00000018, 0x00050048,
        0x0000001a, 0x00000007, 0x00000023, 0x0000001c, 0x00030047, 0x0000001a, 0x00000002,
        0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
        0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000002, 0x00040020, 0x00000008,
        0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040020,
        0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001,
        0x00040017, 0x0000000d, 0x00000006, 0x00000004, 0x00040015, 0x0000000e, 0x00000020,
        0x00000000, 0x0004002b, 0x0000000e, 0x0000000f, 0x00000001, 0x0004001c, 0x00000010,
        0x00000006, 0x0000000f, 0x0006001e, 0x00000011, 0x0000000d, 0x00000006, 0x00000010,
        0x00000010, 0x00040020, 0x00000012, 0x00000003, 0x00000011, 0x0004003b, 0x00000012,
        0x00000013, 0x00000003, 0x00040015, 0x00000014, 0x00000020, 0x00000001, 0x0004002b,
        0x00000014, 0x00000015, 0x00000000, 0x0004002b, 0x0000000e, 0x00000016, 0x00000000,
        0x00040020, 0x00000017, 0x00000001, 0x00000006, 0x000a001e, 0x0000001a, 0x00000006,
        0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x00000014,
        0x00040020, 0x0000001b, 0x00000009, 0x0000001a, 0x0004003b, 0x0000001b, 0x0000001c,
        0x00000009, 0x0004002b, 0x00000014, 0x0000001d, 0x00000003, 0x00040020, 0x0000001e,
        0x00000009, 0x00000006, 0x0004002b, 0x00000014, 0x00000025, 0x00000001, 0x0004002b,
        0x00000014, 0x0000002e, 0x00000002, 0x0004002b, 0x00000006, 0x00000032, 0x00000000,
        0x0004002b, 0x00000006, 0x00000033, 0x3f800000, 0x00040020, 0x00000035, 0x00000003,
        0x0000000d, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
        0x00000005, 0x0004003d, 0x00000007, 0x0000000c, 0x0000000b, 0x0003003e, 0x00000009,
        0x0000000c, 0x00050041, 0x00000017, 0x00000018, 0x0000000b, 0x00000016, 0x0004003d,
        0x00000006, 0x00000019, 0x00000018, 0x00050041, 0x0000001e, 0x0000001f, 0x0000001c,
        0x0000001d, 0x0004003d, 0x00000006, 0x00000020, 0x0000001f, 0x00050085, 0x00000006,
        0x00000021, 0x00000019, 0x00000020, 0x00050041, 0x0000001e, 0x00000022, 0x0000001c,
        0x00000015, 0x0004003d, 0x00000006, 0x00000023, 0x00000022, 0x00050088, 0x00000006,
        0x00000024, 0x00000021, 0x00000023, 0x00050041, 0x0000001e, 0x00000026, 0x0000001c,
        0x00000025, 0x0004003d, 0x00000006, 0x00000027, 0x00000026, 0x00050081, 0x00000006,
        0x00000028, 0x00000024, 0x00000027, 0x00050041, 0x00000017, 0x00000029, 0x0000000b,
        0x0000000f, 0x0004003d, 0x00000006, 0x0000002a, 0x00000029, 0x00050041, 0x0000001e,
        0x0000002f, 0x0000001c, 0x0000002e, 0x0004003d, 0x00000006, 0x00000030, 0x0000002f,
        0x0008000c, 0x00000006, 0x00000031, 0x00000001, 0x00000032, 0x0000002a, 0x00000020,
        0x00000030, 0x00070050, 0x0000000d, 0x00000034, 0x00000028, 0x00000031, 0x00000032,
        0x00000033, 0x00050041, 0x00000035, 0x00000036, 0x00000013, 0x00000015, 0x0003003e,
        0x00000036, 0x00000034, 0x000100fd, 0x00010038,
};

/*
 * // circle.frag
 * #version 450
 * layout(location = 0) in vec2 vLocalPos;
 * layout(location = 0) out vec4 outColor;
 * layout(push_constant) uniform PushConstants {
 *     float uAspectRatio;
 *     float uOffsetX;
 *     float uOffsetY;
 *     float uRadius;
 *     float uColorR;
 *     float uColorG;
 *     float uColorB;
 *     int uGpuWorkload;
 * } pc;
 * void main() {
 *     vec3 baseColor = vec3(pc.uColorR, pc.uColorG, pc.uColorB);
 *     if (pc.uGpuWorkload <= 0) {
 *         outColor = vec4(baseColor, 1.0);
 *         return;
 *     }
 *     float acc = vLocalPos.x * 0.13 + vLocalPos.y * 0.17;
 *     for (int i = 0; i < 65536; ++i) {
 *         if (i >= pc.uGpuWorkload) break;
 *         acc = sin(acc + float(i) * 0.001) * 0.9999 + 0.0001;
 *     }
 *     outColor = vec4(clamp(baseColor + vec3(acc * 0.0001), 0.0, 1.0), 1.0);
 * }
 */
constexpr uint32_t kCircleFragSpv[] = {
        0x07230203, 0x00010000, 0x000d000a, 0x00000076, 0x00000000, 0x00020011, 0x00000001,
        0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
        0x00000000, 0x00000001, 0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000,
        0x00000024, 0x00000030, 0x00030010, 0x00000004, 0x00000007, 0x00050048, 0x0000000b,
        0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000000b, 0x00000001, 0x00000023,
        0x00000004, 0x00050048, 0x0000000b, 0x00000002, 0x00000023, 0x00000008, 0x00050048,
        0x0000000b, 0x00000003, 0x00000023, 0x0000000c, 0x00050048, 0x0000000b, 0x00000004,
        0x00000023, 0x00000010, 0x00050048, 0x0000000b, 0x00000005, 0x00000023, 0x00000014,
        0x00050048, 0x0000000b, 0x00000006, 0x00000023, 0x00000018, 0x00050048, 0x0000000b,
        0x00000007, 0x00000023, 0x0000001c, 0x00030047, 0x0000000b, 0x00000002, 0x00040047,
        0x00000024, 0x0000001e, 0x00000000, 0x00040047, 0x00000030, 0x0000001e, 0x00000000,
        0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
        0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000003, 0x00040015, 0x0000000a,
        0x00000020, 0x00000001, 0x000a001e, 0x0000000b, 0x00000006, 0x00000006, 0x00000006,
        0x00000006, 0x00000006, 0x00000006, 0x00000006, 0x0000000a, 0x00040020, 0x0000000c,
        0x00000009, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000009, 0x0004002b,
        0x0000000a, 0x0000000e, 0x00000004, 0x00040020, 0x0000000f, 0x00000009, 0x00000006,
        0x0004002b, 0x0000000a, 0x00000012, 0x00000005, 0x0004002b, 0x0000000a, 0x00000015,
        0x00000006, 0x0004002b, 0x0000000a, 0x00000019, 0x00000007, 0x00040020, 0x0000001a,
        0x00000009, 0x0000000a, 0x0004002b, 0x0000000a, 0x0000001d, 0x00000000, 0x00020014,
        0x0000001e, 0x00040017, 0x00000022, 0x00000006, 0x00000004, 0x00040020, 0x00000023,
        0x00000003, 0x00000022, 0x0004003b, 0x00000023, 0x00000024, 0x00000003, 0x0004002b,
        0x00000006, 0x00000026, 0x3f800000, 0x00040017, 0x0000002e, 0x00000006, 0x00000002,
        0x00040020, 0x0000002f, 0x00000001, 0x0000002e, 0x0004003b, 0x0000002f, 0x00000030,
        0x00000001, 0x00040015, 0x00000031, 0x00000020, 0x00000000, 0x0004002b, 0x00000031,
        0x00000032, 0x00000000, 0x00040020, 0x00000033, 0x00000001, 0x00000006, 0x0004002b,
        0x00000006, 0x00000036, 0x3e051eb8, 0x0004002b, 0x00000031, 0x00000038, 0x00000001,
        0x0004002b, 0x00000006, 0x0000003b, 0x3e2e147b, 0x0004002b, 0x0000000a, 0x00000046,
        0x00010000, 0x0004002b, 0x00000006, 0x00000052, 0x3a83126f, 0x0004002b, 0x00000006,
        0x00000056, 0x3f7ff972, 0x0004002b, 0x00000006, 0x00000058, 0x38d1b717, 0x0004002b,
        0x0000000a, 0x0000005b, 0x00000001, 0x0004002b, 0x00000006, 0x00000062, 0x00000000,
        0x0006002c, 0x00000007, 0x00000074, 0x00000062, 0x00000062, 0x00000062, 0x0006002c,
        0x00000007, 0x00000075, 0x00000026, 0x00000026, 0x00000026, 0x00050036, 0x00000002,
        0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x000300f7, 0x0000006a,
        0x00000000, 0x000300fb, 0x00000032, 0x0000006b, 0x000200f8, 0x0000006b, 0x00050041,
        0x0000000f, 0x00000010, 0x0000000d, 0x0000000e, 0x0004003d, 0x00000006, 0x00000011,
        0x00000010, 0x00050041, 0x0000000f, 0x00000013, 0x0000000d, 0x00000012, 0x0004003d,
        0x00000006, 0x00000014, 0x00000013, 0x00050041, 0x0000000f, 0x00000016, 0x0000000d,
        0x00000015, 0x0004003d, 0x00000006, 0x00000017, 0x00000016, 0x00060050, 0x00000007,
        0x00000018, 0x00000011, 0x00000014, 0x00000017, 0x00050041, 0x0000001a, 0x0000001b,
        0x0000000d, 0x00000019, 0x0004003d, 0x0000000a, 0x0000001c, 0x0000001b, 0x000500b3,
        0x0000001e, 0x0000001f, 0x0000001c, 0x0000001d, 0x000300f7, 0x00000021, 0x00000000,
        0x000400fa, 0x0000001f, 0x00000020, 0x00000021, 0x000200f8, 0x00000020, 0x00070050,
        0x00000022, 0x0000002a, 0x00000011, 0x00000014, 0x00000017, 0x00000026, 0x0003003e,
        0x00000024, 0x0000002a, 0x000200f9, 0x0000006a, 0x000200f8, 0x00000021, 0x00050041,
        0x00000033, 0x00000034, 0x00000030, 0x00000032, 0x0004003d, 0x00000006, 0x00000035,
        0x00000034, 0x00050041, 0x00000033, 0x00000039, 0x00000030, 0x00000038, 0x0004003d,
        0x00000006, 0x0000003a, 0x00000039, 0x00050085, 0x00000006, 0x0000003c, 0x0000003a,
        0x0000003b, 0x0008000c, 0x00000006, 0x0000003d, 0x00000001, 0x00000032, 0x00000035,
        0x00000036, 0x0000003c, 0x000200f9, 0x00000040, 0x000200f8, 0x00000040, 0x000700f5,
        0x00000006, 0x00000071, 0x0000003d, 0x00000021, 0x00000059, 0x00000043, 0x000700f5,
        0x0000000a, 0x00000070, 0x0000001d, 0x00000021, 0x0000005c, 0x00000043, 0x000500b1,
        0x0000001e, 0x00000047, 0x00000070, 0x00000046, 0x000400f6, 0x00000042, 0x00000043,
        0x00000000, 0x000400fa, 0x00000047, 0x00000041, 0x00000042, 0x000200f8, 0x00000041,
        0x000500af, 0x0000001e, 0x0000004b, 0x00000070, 0x0000001c, 0x000300f7, 0x0000004d,
        0x00000000, 0x000400fa, 0x0000004b, 0x0000004c, 0x0000004d, 0x000200f8, 0x0000004c,
        0x000200f9, 0x00000042, 0x000200f8, 0x0000004d, 0x0004006f, 0x00000006, 0x00000051,
        0x00000070, 0x0008000c, 0x00000006, 0x00000054, 0x00000001, 0x00000032, 0x00000051,
        0x00000052, 0x00000071, 0x0006000c, 0x00000006, 0x00000055, 0x00000001, 0x0000000d,
        0x00000054, 0x0008000c, 0x00000006, 0x00000059, 0x00000001, 0x00000032, 0x00000055,
        0x00000056, 0x00000058, 0x000200f9, 0x00000043, 0x000200f8, 0x00000043, 0x00050080,
        0x0000000a, 0x0000005c, 0x00000070, 0x0000005b, 0x000200f9, 0x00000040, 0x000200f8,
        0x00000042, 0x00050085, 0x00000006, 0x0000005f, 0x00000071, 0x00000058, 0x00060050,
        0x00000007, 0x00000060, 0x0000005f, 0x0000005f, 0x0000005f, 0x00050081, 0x00000007,
        0x00000061, 0x00000018, 0x00000060, 0x0008000c, 0x00000007, 0x00000065, 0x00000001,
        0x0000002b, 0x00000061, 0x00000074, 0x00000075, 0x00050051, 0x00000006, 0x00000066,
        0x00000065, 0x00000000, 0x00050051, 0x00000006, 0x00000067, 0x00000065, 0x00000001,
        0x00050051, 0x00000006, 0x00000068, 0x00000065, 0x00000002, 0x00070050, 0x00000022,
        0x00000069, 0x00000066, 0x00000067, 0x00000068, 0x00000026, 0x0003003e, 0x00000024,
        0x00000069, 0x000200f9, 0x0000006a, 0x000200f8, 0x0000006a, 0x000100fd, 0x00010038,
};

std::array<CircleVertex, 108> BuildCircleVertices() {
    std::array<CircleVertex, 108> vertices{};
    constexpr float kTwoPi = static_cast<float>(2.0 * M_PI);
    for (uint32_t i = 0; i < 36; ++i) {
        const float theta0 = kTwoPi * static_cast<float>(i) / 36.0f;
        const float theta1 = kTwoPi * static_cast<float>(i + 1) / 36.0f;
        vertices[i * 3 + 0] = {0.0f, 0.0f};
        vertices[i * 3 + 1] = {std::cos(theta0), std::sin(theta0)};
        vertices[i * 3 + 2] = {std::cos(theta1), std::sin(theta1)};
    }
    return vertices;
}

} // namespace

bool VulkanCircleRenderer::Initialize(VulkanContext& context, const VulkanSwapchain& swapchain) {
    Destroy();
    VkDevice device = context.GetDevice();

    const auto vertices = BuildCircleVertices();
    mVertexBuffer = VulkanBuffer::CreateVertexBuffer(context, vertices.data(), sizeof(vertices));
    if (!mVertexBuffer.IsValid()) {
        return false;
    }

    mRenderPass =
            VulkanRenderPass::CreateSimplePresentRenderPass(device, swapchain.GetImageFormat());
    if (!mRenderPass.IsValid()) {
        Destroy();
        return false;
    }

    const VkPushConstantRange pushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(CirclePushConstants),
    };
    mPipelineLayout = VulkanPipeline::
            CreatePipelineLayout(device,
                                 std::span<const VkPushConstantRange>(&pushConstantRange, 1));
    if (!mPipelineLayout.IsValid()) {
        Destroy();
        return false;
    }

    ScopedShaderModule vertModule = VulkanPipeline::CreateShaderModule(device, kCircleVertSpv);
    ScopedShaderModule fragModule = VulkanPipeline::CreateShaderModule(device, kCircleFragSpv);
    if (!vertModule.IsValid() || !fragModule.IsValid()) {
        Destroy();
        return false;
    }

    const VkVertexInputBindingDescription bindingDesc{
            .binding = 0,
            .stride = sizeof(CircleVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attrDesc{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(CircleVertex, x),
    };

    VulkanPipelineBuilder builder;
    builder.SetShaderStages(vertModule.Get(), fragModule.Get())
            .SetVertexInput(std::span<const VkVertexInputBindingDescription>(&bindingDesc, 1),
                            std::span<const VkVertexInputAttributeDescription>(&attrDesc, 1))
            .SetInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .SetRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
            .SetLayout(mPipelineLayout.Get())
            .SetRenderPass(mRenderPass.Get(), 0);
    mPipeline = builder.Build(device);
    if (!mPipeline.IsValid()) {
        Destroy();
        return false;
    }

    RecreateFramebuffers(device, swapchain);
    if (mFramebuffers.empty()) {
        Destroy();
        return false;
    }
    return true;
}

void VulkanCircleRenderer::RecreateFramebuffers(VkDevice device, const VulkanSwapchain& swapchain) {
    mFramebuffers =
            VulkanRenderPass::CreateSwapchainFramebuffers(device, mRenderPass.Get(), swapchain);
}

void VulkanCircleRenderer::Destroy() noexcept {
    mFramebuffers.clear();
    mPipeline.Reset();
    mPipelineLayout.Reset();
    mRenderPass.Reset();
    mVertexBuffer.Reset();
}

void VulkanCircleRenderer::RecordDraw(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                      VkExtent2D extent, float aspectRatio,
                                      const std::vector<samples::Circle>& circles,
                                      int32_t gpuWorkload, VulkanOverlayRenderer* overlayRenderer,
                                      size_t frameSlot,
                                      const samples::OverlayBatcher* overlayBatcher) const {
    assert(commandBuffer != VK_NULL_HANDLE);
    assert(imageIndex < mFramebuffers.size());

    VkClearValue clearColor = {.color = {.float32 = {0.12f, 0.14f, 0.18f, 1.0f}}};
    VkRenderPassBeginInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = mRenderPass.Get(),
            .framebuffer = mFramebuffers[imageIndex].Get(),
            .renderArea = {.offset = {0, 0}, .extent = extent},
            .clearValueCount = 1,
            .pClearValues = &clearColor,
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    RecordCircles(commandBuffer, extent, aspectRatio, circles, gpuWorkload);

    if (overlayRenderer != nullptr && overlayBatcher != nullptr) {
        overlayRenderer->RecordDraw(commandBuffer, frameSlot, extent, *overlayBatcher);
    }

    vkCmdEndRenderPass(commandBuffer);
}

void VulkanCircleRenderer::RecordCircles(VkCommandBuffer commandBuffer, VkExtent2D extent,
                                         float aspectRatio,
                                         const std::vector<samples::Circle>& circles,
                                         int32_t gpuWorkload) const {
    SetViewportAndScissor(commandBuffer, extent);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.Get());
    const VkBuffer vertexBuffers[] = {mVertexBuffer.GetBuffer()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    const int32_t clampedWorkload = (gpuWorkload < 0)
            ? 0
            : ((gpuWorkload > samples::Circle::kMaxGpuIterations)
                       ? samples::Circle::kMaxGpuIterations
                       : gpuWorkload);

    for (const auto& circle : circles) {
        const CirclePushConstants pc{
                .uAspectRatio = aspectRatio,
                .uOffsetX = circle.x,
                .uOffsetY = -circle.y,
                .uRadius = circle.radius,
                .uColorR = circle.color.r,
                .uColorG = circle.color.g,
                .uColorB = circle.color.b,
                .uGpuWorkload = clampedWorkload,
        };
        vkCmdPushConstants(commandBuffer, mPipelineLayout.Get(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc),
                           &pc);
        vkCmdDraw(commandBuffer, kVertexCount, 1, 0, 0);
    }
}

void VulkanCircleRenderer::RecordOffscreenDraw(VkCommandBuffer commandBuffer,
                                               VkRenderPass renderPass, VkFramebuffer framebuffer,
                                               VkExtent2D extent, float aspectRatio,
                                               const std::vector<samples::Circle>& circles,
                                               int32_t gpuWorkload) const {
    assert(commandBuffer != VK_NULL_HANDLE);
    assert(renderPass != VK_NULL_HANDLE);
    assert(framebuffer != VK_NULL_HANDLE);

    VkClearValue clearColor = {.color = {.float32 = {0.12f, 0.14f, 0.18f, 1.0f}}};
    VkRenderPassBeginInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderPass,
            .framebuffer = framebuffer,
            .renderArea = {.offset = {0, 0}, .extent = extent},
            .clearValueCount = 1,
            .pClearValues = &clearColor,
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    RecordCircles(commandBuffer, extent, aspectRatio, circles, gpuWorkload);
    vkCmdEndRenderPass(commandBuffer);
}

} // namespace samples::vulkan
