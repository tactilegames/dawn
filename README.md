# Dawn for Android with GLES fixes

Android builds of Google's [Dawn](https://dawn.googlesource.com/dawn) (WebGPU) with fixes to its
OpenGL ES backend that upstream has not taken yet. This repository holds no source; each
[release](../../releases) is one build.

## What a release contains

- `dawn-android-<rev>.tar.gz`, laid out like Google's own `dawn-android-<rev>.tar.gz` release asset:
  - `arm64-v8a/`, `armeabi-v7a/`, `x86/`, `x86_64/`: the static `libwebgpu_dawn.a` for each ABI,
    built from [google/dawn](https://github.com/google/dawn) at `<rev>` with Dawn's own mobile CMake
    recipe (android-26)
  - `patches/`: the patches applied on top of `<rev>`, as `git format-patch` files
  - `licenses/`: Dawn's license and the license notices of every third-party component compiled into
    the libraries
- `SHA256SUMS`
- `LICENSE`: Dawn's license

Use the headers from Google's `dawn-headers-<rev>.tar.gz` for the same revision: the patches change
no public header.

## The fixes

All in `src/dawn/native/opengl/`; Vulkan, Metal and D3D are unchanged.

1. Keep the swapchain's blit framebuffer across presents. Some drivers (PowerVR GE8320) block in
   `glDeleteFramebuffers` until the GPU has finished the frame.
2. Apply the swap interval to the window surface, where `eglSwapInterval` takes effect.
3. Upload tightly packed compressed 2D textures in one call instead of one per block row.

Each is meant for upstream. Once upstream has them all, this repository stops publishing.

## License

Dawn is BSD-3-Clause, with Apache-2.0 for some of its files; see [LICENSE](LICENSE). The patches are
offered under the same terms. Third-party components keep their own licenses, listed in each
release's `licenses/` folder.
