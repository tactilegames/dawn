// Copyright 2017 The Dawn & Tint Authors
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

#ifndef SRC_DAWN_NATIVE_OPENGL_TEXTUREGL_H_
#define SRC_DAWN_NATIVE_OPENGL_TEXTUREGL_H_

#include "partition_alloc/pointers/raw_ptr.h"
#include "src/dawn/native/Texture.h"
#include "src/dawn/native/opengl/opengl_platform.h"

namespace dawn::native::opengl {

class Device;
struct GLFormat;
struct OpenGLFunctions;
class SharedTextureMemory;

enum class OwnsHandle : uint8_t {
    Yes,
    No,
};

class Texture;

// A swapchain texture drawn straight into its EGL window surface (FBO 0). Render passes that can
// target the window do; the others, and copies, use the backing texture, and the content moves
// between the two by blits when a pass loads it or a copy reads it. Both keep Dawn's storage order,
// so the window shows the frame bottom-up (Toggle::GLDrawSwapChainIntoWindow).
struct DefaultFramebufferAlias {
    DefaultFramebufferAlias();
    ~DefaultFramebufferAlias();

    GLuint backingTexture = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool windowHasDepthStencil = false;
    GLuint copyFramebuffer = 0;
    bool loggedTexturePass = false;

    // Per frame: where the frame's content is current.
    bool contentInWindow = false;
    bool contentInTexture = false;
    // The depth-stencil texture whose content lives in the window's depth-stencil buffer.
    Ref<Texture> depthStencil;
};

MaybeError CopyWindowColor(const OpenGLFunctions& gl, DefaultFramebufferAlias* alias, bool toWindow);
// Copies the window's depth-stencil into the aliased depth texture when its content is kept, and
// stops aliasing it.
MaybeError ReleaseWindowDepthStencil(const OpenGLFunctions& gl, DefaultFramebufferAlias* alias);

class Texture final : public TextureBase {
  public:
    static ResultOrError<Ref<Texture>> Create(Device* device,
                                              const UnpackedPtr<TextureDescriptor>& descriptor);
    static ResultOrError<Ref<Texture>> CreateFromSharedTextureMemory(
        SharedTextureMemory* memory,
        const UnpackedPtr<TextureDescriptor>& descriptor);

    Texture(Device* device,
            const UnpackedPtr<TextureDescriptor>& descriptor,
            GLuint handle,
            OwnsHandle ownsHandle);

    GLuint GetTextureHandle() const;
    GLuint GetRenderbufferHandle() const;
    GLenum GetGLTarget() const;
    const GLFormat& GetGLFormat() const;

    MaybeError EnsureSubresourceContentInitialized(const OpenGLFunctions& gl,
                                                   const SubresourceRange& range);

    MaybeError SynchronizeTextureBeforeUse();
    bool IsRenderbuffer() const;

    void SetDefaultFramebufferAlias(DefaultFramebufferAlias* alias);
    DefaultFramebufferAlias* GetDefaultFramebufferAlias() const;
    // The alias whose window depth-stencil buffer holds this depth texture's content, if any.
    void SetWindowDepthStencilOf(DefaultFramebufferAlias* alias);
    DefaultFramebufferAlias* GetWindowDepthStencilOf() const;
    // Before a copy reads the swapchain texture.
    MaybeError SyncFromWindow(const OpenGLFunctions& gl);

  private:
    ~Texture() override;

    void DestroyImpl(DestroyReason reason) override;
    MaybeError ClearTexture(const OpenGLFunctions& gl,
                            const SubresourceRange& range,
                            TextureBase::ClearValue clearValue);

    GLuint mTextureHandle;
    GLuint mRenderbufferHandle;
    OwnsHandle mOwnsHandle = OwnsHandle::No;
    GLenum mTarget;
    // Owned by the swapchain, which clears both before the frame's texture is destroyed.
    raw_ptr<DefaultFramebufferAlias> mDefaultFramebufferAlias = nullptr;
    raw_ptr<DefaultFramebufferAlias> mWindowDepthStencilOf = nullptr;
};

class TextureView final : public TextureViewBase {
  public:
    static ResultOrError<Ref<TextureView>> Create(
        TextureBase* texture,
        const UnpackedPtr<TextureViewDescriptor>& descriptor);

    GLuint GetTextureHandle() const;
    GLuint GetRenderbufferHandle() const;
    GLenum GetGLTarget() const;
    MaybeError BindToFramebuffer(const OpenGLFunctions& gl,
                                 GLenum target,
                                 GLenum attachment,
                                 GLuint layer = 0,
                                 std::optional<uint32_t> passSampleCount = std::nullopt);

  private:
    TextureView(TextureBase* texture, const UnpackedPtr<TextureViewDescriptor>& descriptor);

    ~TextureView() override;
    void DestroyImpl(DestroyReason reason) override;
    GLenum GetInternalFormat() const;

    GLuint mTextureHandle = 0;
    GLuint mRenderbufferHandle = 0;
    GLenum mTarget;
    OwnsHandle mOwnsHandle = OwnsHandle::No;
};

}  // namespace dawn::native::opengl

#endif  // SRC_DAWN_NATIVE_OPENGL_TEXTUREGL_H_
