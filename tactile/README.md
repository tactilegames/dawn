# Tactile's Dawn for Android

This branch is Google's [Dawn](https://dawn.googlesource.com/dawn) at the revision in
`tactile/release.json` (`upstreamRev`), with fixes to its OpenGL ES backend that upstream has not
taken yet. The [Game Engine](https://github.com/tactilegames/game-engine) links Android builds of it;
every other platform uses Google's release.

## Branches and releases

- `gles/<first 8 of the upstream rev>`: one branch per upstream revision. Its first commit is
  Google's; ours follow, one commit per fix, plus the tooling under `tactile/` and
  `.github/workflows/tactile-android.yml`. An older branch is never rewritten, so every release
  stays rebuildable.
- `master` holds only the repository's README.
- A pull request into a `gles/*` branch builds every ABI (`tactile-android` workflow), without
  publishing.
- A merge publishes the merged commit as release `dawn-android-<first 8>-gles.<n>`, `n` one above the
  branch's last release. A release is never replaced.
- The engine uses a release only once a PR there pins it: in `third_party/pins.json`, the `android`
  asset's `repo` is `tactilegames/dawn`, its `tag` is the release, and its sha256 comes from
  `node third_party/fetch.mjs dawn --target android --record`.

| Fix | Ticket | Measured |
|---|---|---|
| keep the swapchain's blit framebuffer across presents | ENGINE-492 | Galaxy A10s (PowerVR GE8320): present 20.1 → 4.5 ms; Adreno 730, Mali-G52: no change |
| apply the swap interval to the window surface | ENGINE-494 | not alone |
| upload tightly packed compressed 2D textures in one call | ENGINE-493 | A10s: 516² ASTC page 28 → 1.4 ms. Galaxy A03s (GE8320): no more driver crashes in strip uploads (Game Stars, 0 of 8 launches vs 4 of 5) |

None is upstream yet. Sending them needs Google's CLA (Tactile's corporate CLA) and an author it
covers. The commits carry a placeholder author.

## Build locally

You need cmake, ninja, git, python3, jq and the NDK in `tactile/release.json`
(`sdkmanager "ndk;<ndkVersion>"`, or the Game Engine's `gameengine setup --platform android`, which
pins the same one). The first build of one ABI takes about an hour; a rebuild after an edit takes
minutes.

```sh
git clone --branch gles/9de0fd67 https://github.com/tactilegames/dawn.git
cd dawn
tactile/build-android.sh armeabi-v7a ~/dawn-install/armeabi-v7a
```

The script fetches only the dependencies the build needs (`tools/fetch_dawn_dependencies.py`), not
every submodule. Build a game against it, one ABI at a time:

```sh
GAMEENGINE_DAWN_DIR=~/dawn-install/armeabi-v7a gameengine build android --project <game> --config probe --abi armeabi-v7a
```

The engine relinks when the library in `GAMEENGINE_DAWN_DIR` changes. Keep one folder per variant
you compare. To check which Dawn an APK carries, compare the hash of its
`lib/<abi>/libgameengine_webgpu.so`.

## Change a fix

Work on a branch from `gles/<rev>` and open a pull request into it. Follow Dawn's style: its
`.clang-format`, 100 columns, and comments that say why, written for Dawn's reviewers. Measure on a
device (below) before merging. The merge publishes the next release.

## Move to a new Dawn revision

1. Check each fix against Dawn main. Drop the ones upstream has taken.
2. Create `gles/<new first 8>` from Google's revision (push the plain upstream commit, which runs
   nothing).
3. In a pull request into it, cherry-pick our remaining commits from the old branch and update
   `upstreamRev` (and `ndkVersion`, if the engine's NDK moved) in `tactile/release.json`.
4. Its merge publishes `.1`. An engine PR then moves the whole Dawn pin (Google's assets for the
   other platforms, this release for Android).

When upstream has every fix, the engine's `android` asset drops its `repo` and `tag`, and this repo
stops publishing.

## Measure on a device

The fixes only matter on the GLES backend, the `compat` tier, which phones without usable Vulkan
run. The reference is the Galaxy A10s (PowerVR GE8320, phone-low); also check one Mali and one Adreno
phone for regressions.

- Run on GLES: `gameengine device run --backend gles`. The `[perf] setup:` log line names the tier.
- Fix the conditions: a device profile with a fixed render scale and no thermal steps, and the frame
  rate unlocked, or the governor changes the scene under you.
- Compare builds A/B/A/B/A/B, at least three rounds each, with a pause between them. Single runs
  drift by more than these fixes change on most GPUs.
- Read present time per frame, not only fps. `gameengine call profile.start`, a few seconds of play,
  then `gameengine call profile.stop --json` gives the render thread's `render.acquire`,
  `render.encode` and `present` sections.

## Tests

`node --test tactile/*.test.mjs` (the license collector). The workflow runs them first.
