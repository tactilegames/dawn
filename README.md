# Tactile's Dawn fork

Google's [Dawn](https://dawn.googlesource.com/dawn) (WebGPU) with fixes upstream has not taken yet,
most of them to its OpenGL ES backend, and Android and Windows x64 builds of it.

## Branches

- `gles/<first 8 of the upstream revision>`: Google's Dawn at that revision, then our commits, one
  per fix, plus the release tooling. One branch per upstream revision the engine pins; older
  branches stay as they are, so every release can be rebuilt. Start with `tactile/README.md` on the
  newest branch.
- `master`: this README only.

## Releases

A pull request into a `gles/*` branch builds every Android ABI and Windows x64, and runs our end2end
tests on Windows' software adapter. A merge that leaves the branch with changes to Dawn itself (not
only the tooling), other than one changing only `tactile/*.md`, publishes the merged commit as [release](../../releases)
`dawn-<first 8>-tactile.<n>` (Android-only releases before that were named
`dawn-android-<first 8>-gles.<n>`; the count continues). A release is never replaced. Each release
contains:

- `dawn-android-<rev>.tar.gz`, laid out like Google's own `dawn-android-<rev>.tar.gz` release asset:
  - `arm64-v8a/`, `armeabi-v7a/`, `x86/`, `x86_64/`: the static `libwebgpu_dawn.a` for each ABI,
    built with Dawn's own mobile CMake recipe (android-26)
  - `patches/`: our changes on top of `<rev>`, as `git format-patch` files
  - `licenses/`: Dawn's license and the license notices of every third-party component compiled into
    the libraries
- `Dawn-<rev>-windows-latest-Release.tar.gz`, laid out like Google's own asset of that name and built
  with Dawn's own Windows recipe (GitHub's `windows-latest`, Release) and MSVC 14.44, so any MSVC from
  14.44 on links it, plus `licenses/`; it includes Dawn's headers (`include/`)
- `SHA256SUMS`
- `LICENSE`: Dawn's license

For Android, use the headers from Google's `dawn-headers-<rev>.tar.gz` for the same revision; the
Windows archive has its own. The fixes change no public header.

## The fixes

| Fix | Backends | Since |
|---|---|---|
| Keep the swapchain's blit framebuffer across presents. Some drivers (PowerVR GE8320) block in `glDeleteFramebuffers` until the GPU has finished the frame. | OpenGL ES, OpenGL (EGL) | `gles.1` |
| Upload tightly packed compressed textures one image per call instead of one per block row. | OpenGL ES | `gles.1` |
| Lazy-clear each mip level with its own row pitch. | OpenGL ES, OpenGL | `gles.1` |
| Reuse a direct Dispatch's synchronization scope when its bind groups and resource table repeat, and forget the cached scope when the compute pass ends. | all | `gles.2` |
| Let an Android swapchain draw straight into its window, behind the device toggle `gl_draw_swapchain_into_window`. It applies when the surface's usage is at most RenderAttachment and CopySrc; the window then shows the frame's rows bottom-up, so an app using it draws its frames mirrored vertically. | OpenGL ES | `gles.3` |
| Offer an sRGB window surface without `EGL_KHR_no_config_context` where `EGL_KHR_gl_colorspace` is present. | OpenGL ES, OpenGL (EGL) | `gles.3` |
| Without validation, look up each rewritten indirect draw's arguments by its own index. D3D12 rewrites only draws whose pipeline reads `vertex_index` or `instance_index`, OpenGL only indexed ones; a pass that also had plain indirect draws read past the rewritten ones and crashed (crbug.com/495489174). The bookkeeping changed in D3D11, D3D12, Metal, Vulkan and OpenGL, the backends that use it; only the listed ones rewrite draws without validation. | D3D12, OpenGL, OpenGL ES | `tactile.4` |
| Without validation, rewrite the indirect draws of a render bundle too: a bundle's draws that needed rewriting were never rewritten, and the backend looked up arguments nobody wrote. | D3D12, OpenGL, OpenGL ES | `tactile.4` |

Each is meant for upstream. Once upstream has them all, this repository stops publishing.

## License

Dawn is BSD-3-Clause, with Apache-2.0 for some of its files; see [LICENSE](LICENSE). Our changes are
offered under the same terms. Third-party components keep their own licenses, listed in each
release's `licenses/` folder.
