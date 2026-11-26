# Dawn for all platforms with our fixes

This branch is Google's [Dawn](https://dawn.googlesource.com/dawn) at the revision in
`tactile/release.json` (`upstreamRev`), with fixes that upstream has not taken yet, and the tooling
that builds and publishes it for every platform: Android, Apple xcframework (iOS + macOS), Linux
x64, macOS (arm64 + x64), Windows x64, and headers.

## Branches and releases

- `gles/<first 8 of the upstream rev>`: one branch per upstream revision. Its first commit is
  Google's; ours follow, one commit per fix, plus the tooling under `tactile/` and
  `.github/workflows/tactile-release.yml`. An older branch is never rewritten, so every release
  stays rebuildable.
- `master` holds only the repository's README.
- A pull request into a `gles/*` branch builds every platform (`tactile-release` workflow), without
  publishing, and runs the end2end tests listed in `release.json` (`windowsTests`) on Windows'
  software adapter (WARP) with D3D12 and D3D11.
- A merge publishes the merged commit as release `dawn-<first 8>-tactile.<n>`, `n` one above the
  branch's last release (older Android-only releases are named `dawn-android-<first 8>-gles.<n>` and
  share the count). A release is never replaced. While a branch carries only this tooling and no
  change to Dawn itself, its build would be Google's, so a merge publishes nothing.
- Each release publishes the full set of assets matching Google's filenames and layouts (each plus a
  `licenses/` folder):
  - `dawn-android-<rev>.tar.gz`
  - `dawn-headers-<rev>.tar.gz`
  - `dawn-apple-<rev>.xcframework.tar.gz`
  - `Dawn-<rev>-ubuntu-latest-Release.tar.gz` (manylinux_2_28 / glibc 2.28)
  - `Dawn-<rev>-macos-latest-Release.tar.gz` (arm64)
  - `Dawn-<rev>-macos-15-intel-Release.tar.gz` (x64)
  - `Dawn-<rev>-windows-latest-Release.tar.gz` (MSVC 14.44 so any MSVC from 14.44 on links it)
  Users can point a single release pin at `dawn-<first 8>-tactile.<n>` across every platform.

Each fix is meant for upstream. Sending one needs Google's CLA and an author it covers.

## Build locally

You need cmake, ninja, git, python3, jq and the NDK in `tactile/release.json`
(`sdkmanager "ndk;<ndkVersion>"`). The first build of one ABI takes about an hour; a rebuild after
an edit takes minutes.

```sh
git clone --branch gles/9de0fd67 https://github.com/tactilegames/dawn.git
cd dawn
tactile/build-android.sh armeabi-v7a ~/dawn-install/armeabi-v7a
```

The script fetches only the dependencies the build needs (`tools/fetch_dawn_dependencies.py`), not
every submodule. The install folder holds `include/` and `lib/libwebgpu_dawn.a` for that ABI.

## Change a fix

Work on a branch from `gles/<rev>` and open a pull request into it. Follow Dawn's style: its
`.clang-format`, 100 columns, and comments that say why, written for Dawn's reviewers. Measure on a
device before merging. The merge publishes the next release.

## Move to a new Dawn revision

1. Check each fix against Dawn main. Drop the ones upstream has taken.
2. Create `gles/<new first 8>` from Google's revision (push the plain upstream commit, which runs
   nothing).
3. In a pull request into it, cherry-pick the tooling and our remaining commits from the old branch,
   and update `upstreamRev` (and `ndkVersion`, if needed) in `tactile/release.json`.
4. Its merge publishes `.1`.

When upstream has every fix, this repository stops publishing.

## Build for Windows locally

From a Developer Command Prompt for VS (x64), with cmake, ninja and python3:

```bat
cmake -G Ninja -S . -B out/windows-x64 -C .github/workflows/dawn-ci.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build out/windows-x64
cmake --install out/windows-x64 --prefix %USERPROFILE%\dawn-install\windows-x64
```

`dawn-ci.cmake` reads `WIN10_SDK_VERSION` and `WIN10_SDK_PATH` (see the workflow). Keep Dawn's default test
options, as the workflow does: the install lists Tint libraries that only Tint's tests build. A build
folder configured with tests off needs a fresh one.

## Tests

- `node --test tactile/*.test.mjs` (the license collector). The workflow runs them first.
- End2end tests for our fixes live beside Dawn's own (`src/dawn/tests/end2end/`). On a pull request
  the workflow runs `windowsTests` from `release.json` on WARP with asserts on, and fails unless each
  test in `windowsMustRun` ran and passed (a test with no adapter is skipped, not failed). On Linux:
  build `dawn_end2end_tests` and run it with `--backend=vulkan,opengles`.
