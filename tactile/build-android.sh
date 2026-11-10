#!/bin/bash
# Builds this checkout for one Android ABI with Dawn's own mobile recipe (.github/workflows/ci.yml,
# mobile-android) and installs it in the layout the Game Engine's GAMEENGINE_DAWN_DIR reads.
# .github/workflows/tactile-android.yml runs it for releases; tactile/README.md says how to use it
# for local work.
#
#   tactile/build-android.sh <abi> <install dir>
#
# The NDK is ANDROID_NDK_HOME, or tactile/release.json's ndkVersion in the Android SDK. It must be
# that version: the Game Engine links the library with the same NDK.
set -euo pipefail

ABI="${1:?usage: tactile/build-android.sh <abi> <install dir>; abi: arm64-v8a, armeabi-v7a, x86 or x86_64}"
mkdir -p "${2:?install dir}"
PREFIX="$(cd "$2" && pwd)"
SRC="$(cd "$(dirname "$0")/.." && pwd)"

case "$(uname -s)" in
	Darwin) HOST=darwin-x86_64 DEFAULT_SDK="$HOME/Library/Android/sdk" ;;
	*) HOST=linux-x86_64 DEFAULT_SDK="$HOME/Android/Sdk" ;;
esac
NDK_VERSION=$(jq -r .ndkVersion "$SRC/tactile/release.json")
PLATFORM=$(jq -r .androidPlatform "$SRC/tactile/release.json")
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
	ANDROID_NDK_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$DEFAULT_SDK}}/ndk/$NDK_VERSION"
fi
found=$(sed -n 's/^Pkg.Revision *= *//p' "$ANDROID_NDK_HOME/source.properties" 2>/dev/null || true)
if [ "$found" != "$NDK_VERSION" ]; then
	echo "NDK at $ANDROID_NDK_HOME is '${found:-missing}', tactile/release.json pins $NDK_VERSION" >&2
	exit 1
fi

cd "$SRC"
OUT="out/android-$ABI"

# Only the dependencies the build needs, not every submodule.
if [ ! -f third_party/abseil-cpp/CMakeLists.txt ]; then
	echo "== Dawn's dependencies"
	python3 tools/fetch_dawn_dependencies.py
fi

# Rebuilt when the protobuf sources move (a new Dawn rev): generated code must match its protoc.
protobuf=$(git -C third_party/protobuf rev-parse HEAD 2>/dev/null || echo unknown)
if [ ! -x out/host/protoc ] || [ "$(cat out/host/protobuf.rev 2>/dev/null)" != "$protobuf" ]; then
	echo "== host protoc"
	rm -rf out/host
	cmake -G Ninja -S . -B out/host \
		-DDAWN_BUILD_PROTOBUF=ON -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_NODE=OFF \
		-DDAWN_ENABLE_VULKAN=OFF -DDAWN_ENABLE_METAL=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_D3D11=OFF \
		-DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_NULL=OFF \
		-DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF -DDAWN_USE_GLFW=OFF -DDAWN_BUILD_TESTS=OFF \
		-DTINT_BUILD_CMD_TOOLS=OFF -DTINT_BUILD_DOCS=OFF -DTINT_BUILD_TESTS=OFF \
		-DTINT_BUILD_WGSL_READER=OFF -DTINT_BUILD_WGSL_WRITER=OFF -DTINT_BUILD_GLSL_VALIDATOR=OFF \
		-DTINT_BUILD_IR_BINARY=OFF -DCMAKE_BUILD_TYPE=Release
	cmake --build out/host --target protoc
	echo "$protobuf" > out/host/protobuf.rev
fi

echo "== Dawn for $ABI"
cmake -S . -B "$OUT" -G Ninja \
	-DDAWN_MOBILE_BUILD=android \
	-C .github/workflows/dawn-ci.cmake \
	-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
	-DANDROID_ABI="$ABI" -DANDROID_PLATFORM="$PLATFORM" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DPROTOC_EXECUTABLE="$SRC/out/host/protoc"
# Set CMAKE_BUILD_PARALLEL_LEVEL on a machine short of memory: Tint's units are heavy.
cmake --build "$OUT"

echo "== install to $PREFIX"
cmake --install "$OUT" --prefix "$PREFIX"
mkdir -p "$PREFIX/lib"
cp "$OUT/src/dawn/native/libwebgpu_dawn.a" "$PREFIX/lib/libwebgpu_dawn.a"
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/llvm-strip" --strip-debug "$PREFIX/lib/libwebgpu_dawn.a"
ls -l "$PREFIX/lib/libwebgpu_dawn.a" "$PREFIX/include/dawn/webgpu.h"
echo "== GAMEENGINE_DAWN_DIR=$PREFIX (build dir $SRC/$OUT)"
