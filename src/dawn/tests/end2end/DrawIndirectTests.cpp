// Copyright 2019 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <vector>

#include "src/dawn/tests/DawnTest.h"
#include "src/dawn/utils/ComboRenderPipelineDescriptor.h"
#include "src/dawn/utils/WGPUHelpers.h"

namespace dawn {
namespace {

constexpr uint32_t kRTSize = 4;
constexpr utils::RGBA8 filled(0, 255, 0, 255);
constexpr utils::RGBA8 notFilled(0, 0, 0, 0);

class DrawIndirectTest : public DawnTest {
  protected:
    void SetUp() override {
        DawnTest::SetUp();

        renderPass = utils::CreateBasicRenderPass(device, kRTSize, kRTSize);

        wgpu::ShaderModule vsModule = utils::CreateShaderModule(device, R"(
            @vertex
            fn main(@location(0) pos : vec4f) -> @builtin(position) vec4f {
                return pos;
            })");

        wgpu::ShaderModule fsModule = utils::CreateShaderModule(device, R"(
            @fragment fn main() -> @location(0) vec4f {
                return vec4f(0.0, 1.0, 0.0, 1.0);
            })");

        utils::ComboRenderPipelineDescriptor descriptor;
        descriptor.vertex.module = vsModule;
        descriptor.cFragment.module = fsModule;
        descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
        descriptor.primitive.stripIndexFormat = wgpu::IndexFormat::Uint32;
        descriptor.vertex.bufferCount = 1;
        descriptor.cBuffers[0].arrayStride = 4 * sizeof(float);
        descriptor.cBuffers[0].attributeCount = 1;
        descriptor.cAttributes[0].format = wgpu::VertexFormat::Float32x4;
        descriptor.cTargets[0].format = renderPass.colorFormat;

        pipeline = device.CreateRenderPipeline(&descriptor);

        vertexBuffer = utils::CreateBufferFromData<float>(
            device, wgpu::BufferUsage::Vertex,
            {// The bottom left triangle
             -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 1.0f,

             // The top right triangle
             -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f});
    }

    utils::BasicRenderPass renderPass;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer vertexBuffer;

    void Test(std::initializer_list<uint32_t> bufferList,
              uint64_t indirectOffset,
              utils::RGBA8 bottomLeftExpected,
              utils::RGBA8 topRightExpected) {
        wgpu::Buffer indirectBuffer =
            utils::CreateBufferFromData<uint32_t>(device, wgpu::BufferUsage::Indirect, bufferList);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        {
            wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass.renderPassInfo);
            pass.SetPipeline(pipeline);
            pass.SetVertexBuffer(0, vertexBuffer);
            pass.DrawIndirect(indirectBuffer, indirectOffset);
            pass.End();
        }

        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);

        EXPECT_PIXEL_RGBA8_EQ(bottomLeftExpected, renderPass.color, 1, 3);
        EXPECT_PIXEL_RGBA8_EQ(topRightExpected, renderPass.color, 3, 1);
    }
};

// The basic triangle draw.
TEST_P(DrawIndirectTest, Uint32) {
    // Test a draw with no indices.
    Test({0, 0, 0, 0}, 0, notFilled, notFilled);

    // Test a draw with only the first 3 indices (bottom left triangle)
    Test({3, 1, 0, 0}, 0, filled, notFilled);

    // Test a draw with only the last 3 indices (top right triangle)
    Test({3, 1, 3, 0}, 0, notFilled, filled);

    // Test a draw with all 6 indices (both triangles).
    Test({6, 1, 0, 0}, 0, filled, filled);
}

TEST_P(DrawIndirectTest, IndirectOffset) {
    // Test an offset draw call, with indirect buffer containing 2 calls:
    // 1) only the first 3 indices (bottom left triangle)
    // 2) only the last 3 indices (top right triangle)

    // Test #1 (no offset)
    Test({3, 1, 0, 0, 3, 1, 3, 0}, 0, filled, notFilled);

    // Offset to draw #2
    Test({3, 1, 0, 0, 3, 1, 3, 0}, 4 * sizeof(uint32_t), notFilled, filled);
}

DAWN_INSTANTIATE_TEST(DrawIndirectTest,
                      D3D11Backend(),
                      D3D12Backend(),
                      MetalBackend(),
                      OpenGLBackend(),
                      OpenGLESBackend(),
                      VulkanBackend(),
                      WebGPUBackend());

class DrawIndirectUsingFirstVertexTest : public DawnTest {
  protected:
    virtual void SetupShaderModule() {
        vsModule = utils::CreateShaderModule(device, R"(
            struct VertexInput {
                @builtin(vertex_index) id : u32,
                @location(0) pos: vec4f,
            };

            @group(0) @binding(0) var<uniform> offset: array<vec4f, 2>;

            @vertex
            fn main(input: VertexInput) -> @builtin(position) vec4f {
                return input.pos + offset[input.id / 3u];
            })");

        fsModule = utils::CreateShaderModule(device, R"(
            @fragment fn main() -> @location(0) vec4f {
                return vec4f(0.0, 1.0, 0.0, 1.0);
            })");
    }

    void GeneralSetup() {
        renderPass = utils::CreateBasicRenderPass(device, kRTSize, kRTSize);

        SetupShaderModule();

        utils::ComboRenderPipelineDescriptor descriptor;
        descriptor.vertex.module = vsModule;
        descriptor.cFragment.module = fsModule;
        descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
        descriptor.primitive.stripIndexFormat = wgpu::IndexFormat::Uint32;
        descriptor.vertex.bufferCount = 1;
        descriptor.cBuffers[0].arrayStride = 4 * sizeof(float);
        descriptor.cBuffers[0].attributeCount = 1;
        descriptor.cAttributes[0].format = wgpu::VertexFormat::Float32x4;
        descriptor.cTargets[0].format = renderPass.colorFormat;

        pipeline = device.CreateRenderPipeline(&descriptor);

        // Offset to the vertices, that needs correcting by the calibration offset from uniform
        // buffer referenced by instance index to get filled triangle on screen.
        constexpr float calibration = 99.0f;

        vertexBuffer = utils::CreateBufferFromData<float>(
            device, wgpu::BufferUsage::Vertex,

            {// The bottom left triangle
             -1.0f - calibration, 1.0f, 0.0f, 1.0f, 1.0f - calibration, -1.0f, 0.0f, 1.0f,
             -1.0f - calibration, -1.0f, 0.0f, 1.0f,

             // The top right triangle
             -1.0f - calibration, 1.0f, 0.0f, 1.0f, 1.0f - calibration, -1.0f, 0.0f, 1.0f,
             1.0f - calibration, 1.0f, 0.0f, 1.0f});

        // Providing calibration vec4f offset values
        wgpu::Buffer uniformBuffer =
            utils::CreateBufferFromData<float>(device, wgpu::BufferUsage::Uniform,
                                               {
                                                   // Bad calibration at [0]
                                                   0.0,
                                                   0.0,
                                                   0.0,
                                                   0.0,
                                                   // Good calibration at [1]
                                                   calibration,
                                                   0.0,
                                                   0.0,
                                                   0.0,
                                               });

        bindGroup =
            utils::MakeBindGroup(device, pipeline.GetBindGroupLayout(0), {{0, uniformBuffer}});
    }

    void SetUp() override {
        DawnTest::SetUp();
        GeneralSetup();
    }

    utils::BasicRenderPass renderPass;
    wgpu::RenderPipeline pipeline;
    wgpu::Buffer vertexBuffer;
    wgpu::BindGroup bindGroup;
    wgpu::ShaderModule vsModule;
    wgpu::ShaderModule fsModule;

    // Test two DrawIndirect calls with different indirect offsets within one pass.
    void Test(std::initializer_list<uint32_t> bufferList,
              utils::RGBA8 bottomLeftExpected,
              utils::RGBA8 topRightExpected) {
        // Indirect buffer contains 2 draw params
        DAWN_ASSERT(bufferList.size() == 8);
        wgpu::Buffer indirectBuffer =
            utils::CreateBufferFromData<uint32_t>(device, wgpu::BufferUsage::Indirect, bufferList);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        {
            wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass.renderPassInfo);
            pass.SetPipeline(pipeline);
            pass.SetVertexBuffer(0, vertexBuffer);
            pass.SetBindGroup(0, bindGroup);
            pass.DrawIndirect(indirectBuffer, 0);
            pass.DrawIndirect(indirectBuffer, 4 * sizeof(uint32_t));
            pass.End();
        }

        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);

        EXPECT_PIXEL_RGBA8_EQ(bottomLeftExpected, renderPass.color, 1, 3);
        EXPECT_PIXEL_RGBA8_EQ(topRightExpected, renderPass.color, 3, 1);
    }
};

TEST_P(DrawIndirectUsingFirstVertexTest, IndirectOffset) {
    // Test an offset draw call, with indirect buffer containing 2 calls:
    // 1) only the first 3 indices (bottom left triangle)
    // 2) only the last 3 indices (top right triangle)

    // #2 draw has the correct offset applied by vertex index
    Test({3, 1, 0, 0, 3, 1, 3, 0}, notFilled, filled);
}

DAWN_INSTANTIATE_TEST(DrawIndirectUsingFirstVertexTest,
                      D3D11Backend(),
                      D3D12Backend(),
                      MetalBackend(),
                      OpenGLBackend(),
                      OpenGLESBackend(),
                      VulkanBackend());

class DrawIndirectUsingInstanceIndexTest : public DrawIndirectUsingFirstVertexTest {
  protected:
    std::vector<wgpu::FeatureName> GetRequiredFeatures() override {
        if (!SupportsFeatures({wgpu::FeatureName::IndirectFirstInstance})) {
            return {};
        }
        return {wgpu::FeatureName::IndirectFirstInstance};
    }

    void SetupShaderModule() override {
        vsModule = utils::CreateShaderModule(device, R"(
            struct VertexInput {
                @builtin(instance_index) id : u32,
                @location(0) pos: vec4f,
            };

            @group(0) @binding(0) var<uniform> offset: array<vec4f, 2>;

            @vertex
            fn main(input: VertexInput) -> @builtin(position) vec4f {
                return input.pos + offset[input.id];
            })");

        fsModule = utils::CreateShaderModule(device, R"(
            @fragment fn main() -> @location(0) vec4f {
                return vec4f(0.0, 1.0, 0.0, 1.0);
            })");
    }

    void SetUp() override {
        dawn::DrawIndirectUsingFirstVertexTest::SetUp();
        DAWN_TEST_UNSUPPORTED_IF(!device.HasFeature(wgpu::FeatureName::IndirectFirstInstance));
    }
};

TEST_P(DrawIndirectUsingInstanceIndexTest, IndirectOffset) {
    // Test an offset draw call, with indirect buffer containing 2 calls:
    // 1) only the first 3 indices (bottom left triangle)
    // 2) only the last 3 indices (top right triangle)

    // Test 1: #1 draw has the correct calibration referenced by instance index
    Test({3, 1, 0, 1, 3, 1, 3, 0}, filled, notFilled);

    // Test 2: #2 draw has the correct offset applied by instance index
    Test({3, 1, 0, 0, 3, 1, 3, 1}, notFilled, filled);
}

DAWN_INSTANTIATE_TEST(DrawIndirectUsingInstanceIndexTest,
                      D3D11Backend(),
                      D3D12Backend(),
                      MetalBackend(),
                      OpenGLBackend(),
                      OpenGLESBackend(),
                      VulkanBackend());

// Indirect draws that a backend must rewrite (D3D12 copies the arguments of a draw whose pipeline
// reads vertex_index or instance_index, OpenGL those of every indexed draw) share a pass with ones
// it encodes as they are. With skip_validation only the rewritten draws are recorded for the
// rewrite, and each must still get its own arguments.
class DrawIndirectMixedRewriteTest : public DawnTest {
  protected:
    void SetUp() override {
        DawnTest::SetUp();

        renderPass = utils::CreateBasicRenderPass(device, kRTSize, kRTSize);

        wgpu::ShaderModule fsModule = utils::CreateShaderModule(device, R"(
            @fragment fn main() -> @location(0) vec4f {
                return vec4f(0.0, 1.0, 0.0, 1.0);
            })");

        utils::ComboRenderPipelineDescriptor descriptor;
        descriptor.cFragment.module = fsModule;
        descriptor.cTargets[0].format = renderPass.colorFormat;

        // Reads its positions from a vertex buffer, so no backend rewrites its draws.
        descriptor.vertex.module = utils::CreateShaderModule(device, R"(
            @vertex
            fn main(@location(0) pos : vec4f) -> @builtin(position) vec4f {
                return pos;
            })");
        descriptor.vertex.bufferCount = 1;
        descriptor.cBuffers[0].arrayStride = 4 * sizeof(float);
        descriptor.cBuffers[0].attributeCount = 1;
        descriptor.cAttributes[0].format = wgpu::VertexFormat::Float32x4;
        plainPipeline = device.CreateRenderPipeline(&descriptor);

        // Picks its positions by vertex_index, so it draws the top right triangle only when the
        // draw's firstVertex (3) reaches the shader.
        descriptor.vertex.module = utils::CreateShaderModule(device, R"(
            @vertex
            fn main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
                var pos = array(vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(-1.0, -1.0),
                                vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0));
                return vec4f(pos[i], 0.0, 1.0);
            })");
        descriptor.vertex.bufferCount = 0;
        vertexIndexPipeline = device.CreateRenderPipeline(&descriptor);

        vertexBuffer = utils::CreateBufferFromData<float>(
            device, wgpu::BufferUsage::Vertex,
            {// The bottom left triangle
             -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 1.0f,

             // The top right triangle
             -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f});
    }

    // Several plain draws of the bottom left triangle, then one vertex_index draw of the top right.
    template <typename Encoder>
    void EncodePlainThenVertexIndex(Encoder pass, wgpu::Buffer indirectBuffer) {
        pass.SetPipeline(plainPipeline);
        pass.SetVertexBuffer(0, vertexBuffer);
        for (uint32_t i = 0; i < kPlainDraws; ++i) {
            pass.DrawIndirect(indirectBuffer, 0);
        }
        pass.SetPipeline(vertexIndexPipeline);
        pass.DrawIndirect(indirectBuffer, 4 * sizeof(uint32_t));
    }

    // Several plain draws of the bottom left triangle, then one indexed draw of the top right.
    template <typename Encoder>
    void EncodePlainThenIndexed(Encoder pass,
                                wgpu::Buffer indirectBuffer,
                                wgpu::Buffer indexBuffer) {
        pass.SetPipeline(plainPipeline);
        pass.SetVertexBuffer(0, vertexBuffer);
        for (uint32_t i = 0; i < kPlainDraws; ++i) {
            pass.DrawIndirect(indirectBuffer, 0);
        }
        pass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);
        pass.DrawIndexedIndirect(indirectBuffer, 4 * sizeof(uint32_t));
    }

    wgpu::RenderBundleEncoder CreateBundleEncoder() {
        wgpu::RenderBundleEncoderDescriptor desc = {};
        desc.colorFormatCount = 1;
        desc.colorFormats = &renderPass.colorFormat;
        return device.CreateRenderBundleEncoder(&desc);
    }

    void ExpectBothTriangles(const wgpu::CommandEncoder& encoder) {
        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);

        EXPECT_PIXEL_RGBA8_EQ(filled, renderPass.color, 1, 3);
        EXPECT_PIXEL_RGBA8_EQ(filled, renderPass.color, 3, 1);
    }

    static constexpr uint32_t kPlainDraws = 8;

    utils::BasicRenderPass renderPass;
    wgpu::RenderPipeline plainPipeline;
    wgpu::RenderPipeline vertexIndexPipeline;
    wgpu::Buffer vertexBuffer;
};

// Plain indirect draws before a vertex_index one in the same pass.
TEST_P(DrawIndirectMixedRewriteTest, PlainDrawsBeforeVertexIndexDraw) {
    wgpu::Buffer indirectBuffer = utils::CreateBufferFromData<uint32_t>(
        device, wgpu::BufferUsage::Indirect, {3, 1, 0, 0, 3, 1, 3, 0});

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass.renderPassInfo);
    EncodePlainThenVertexIndex(pass, indirectBuffer);
    pass.End();

    ExpectBothTriangles(encoder);
}

// The same draws recorded in a render bundle.
TEST_P(DrawIndirectMixedRewriteTest, PlainDrawsBeforeVertexIndexDrawInBundle) {
    wgpu::Buffer indirectBuffer = utils::CreateBufferFromData<uint32_t>(
        device, wgpu::BufferUsage::Indirect, {3, 1, 0, 0, 3, 1, 3, 0});

    wgpu::RenderBundleEncoder bundleEncoder = CreateBundleEncoder();
    EncodePlainThenVertexIndex(bundleEncoder, indirectBuffer);
    wgpu::RenderBundle bundle = bundleEncoder.Finish();

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass.renderPassInfo);
    pass.ExecuteBundles(1, &bundle);
    pass.End();

    ExpectBothTriangles(encoder);
}

// Plain non-indexed indirect draws before an indexed one in the same pass.
TEST_P(DrawIndirectMixedRewriteTest, PlainDrawsBeforeIndexedDraw) {
    wgpu::Buffer indirectBuffer = utils::CreateBufferFromData<uint32_t>(
        device, wgpu::BufferUsage::Indirect, {3, 1, 0, 0, 3, 1, 3, 0, 0});
    wgpu::Buffer indexBuffer =
        utils::CreateBufferFromData<uint32_t>(device, wgpu::BufferUsage::Index, {0, 1, 2, 3, 4, 5});

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass.renderPassInfo);
    EncodePlainThenIndexed(pass, indirectBuffer, indexBuffer);
    pass.End();

    ExpectBothTriangles(encoder);
}

// The same draws recorded in a render bundle.
TEST_P(DrawIndirectMixedRewriteTest, PlainDrawsBeforeIndexedDrawInBundle) {
    wgpu::Buffer indirectBuffer = utils::CreateBufferFromData<uint32_t>(
        device, wgpu::BufferUsage::Indirect, {3, 1, 0, 0, 3, 1, 3, 0, 0});
    wgpu::Buffer indexBuffer =
        utils::CreateBufferFromData<uint32_t>(device, wgpu::BufferUsage::Index, {0, 1, 2, 3, 4, 5});

    wgpu::RenderBundleEncoder bundleEncoder = CreateBundleEncoder();
    EncodePlainThenIndexed(bundleEncoder, indirectBuffer, indexBuffer);
    wgpu::RenderBundle bundle = bundleEncoder.Finish();

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass.renderPassInfo);
    pass.ExecuteBundles(1, &bundle);
    pass.End();

    ExpectBothTriangles(encoder);
}

DAWN_INSTANTIATE_TEST(DrawIndirectMixedRewriteTest,
                      D3D11Backend(),
                      D3D11Backend({"skip_validation"}),
                      D3D12Backend(),
                      D3D12Backend({"skip_validation"}),
                      MetalBackend(),
                      MetalBackend({"skip_validation"}),
                      OpenGLBackend(),
                      OpenGLBackend({"skip_validation"}),
                      OpenGLESBackend(),
                      OpenGLESBackend({"skip_validation"}),
                      VulkanBackend(),
                      VulkanBackend({"skip_validation"}));

}  // anonymous namespace
}  // namespace dawn
