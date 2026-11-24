import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { coveringLicenses, depsPaths, licenseFilesIn, toPosix } from './collect-licenses.mjs';

const tree = {
  'third_party/abseil-cpp': ['LICENSE', 'absl'],
  'third_party/abseil-cpp/absl/base': ['config.h'],
  'third_party/abseil-cpp/absl': ['base'],
  'third_party/vulkan-deps': ['LICENSE', 'spirv-tools'],
  'third_party/vulkan-deps/spirv-tools': ['src'],
  'third_party/vulkan-deps/spirv-tools/src': ['LICENSE', 'CHANGES', 'source'],
  'third_party/vulkan-deps/spirv-tools/src/source': ['opt.cpp'],
  'third_party/khronos/EGL': ['egl.h'],
  'third_party/khronos': ['EGL', 'NOTICE.txt', 'README'],
  'third_party/nolicense/src': ['a.cc'],
  'third_party/nolicense': ['src'],
};
const listDir = (rel) => tree[rel] ?? [];
const root = '/w/dawn';

describe('licenseFilesIn', () => {
  it('keeps license, copying and notice files in any common spelling', () => {
    assert.deepEqual(licenseFilesIn(['LICENSE.txt', 'COPYING', 'NOTICE', 'Licence.md', 'README', 'LICENSES.chromium']), [
      'COPYING',
      'LICENSE.txt',
      'Licence.md',
      'NOTICE',
    ]);
  });
});

describe('coveringLicenses', () => {
  it('maps each compiled third_party file to the nearest folder with a license', () => {
    const { found, unlicensed } = coveringLicenses(
      [
        `${root}/third_party/abseil-cpp/absl/base/config.h`,
        `${root}/third_party/vulkan-deps/spirv-tools/src/source/opt.cpp`,
        `${root}/third_party/khronos/EGL/egl.h`,
        `${root}/src/dawn/native/Device.cpp`,
        '/ndk/sysroot/usr/include/stdio.h',
      ],
      root,
      listDir,
    );
    assert.deepEqual([...found], [
      ['third_party/abseil-cpp', ['LICENSE']],
      ['third_party/khronos', ['NOTICE.txt']],
      ['third_party/vulkan-deps/spirv-tools/src', ['LICENSE']],
    ]);
    assert.deepEqual(unlicensed, []);
  });

  it('reports a dependency whose compiled files no license covers', () => {
    const { unlicensed } = coveringLicenses([`${root}/third_party/nolicense/src/a.cc`], root, listDir);
    assert.deepEqual(unlicensed, ['third_party/nolicense']);
  });
});

describe('depsPaths', () => {
  it('reads the indented header paths of ninja -t deps, resolved against the build folder', () => {
    const text = [
      'src/dawn/native/CMakeFiles/x.dir/Device.cpp.o: #deps 2, deps mtime 1 (VALID)',
      '    ../../src/dawn/native/Device.h',
      '    /ndk/sysroot/usr/include/stdio.h',
      '',
    ].join('\n');
    assert.deepEqual(depsPaths(text, '/w/dawn/out/android'), ['/w/dawn/src/dawn/native/Device.h', '/ndk/sysroot/usr/include/stdio.h']);
  });
});

describe('Windows paths', () => {
  it('turns drive paths into lower-case POSIX ones and leaves POSIX paths alone', () => {
    assert.equal(toPosix('D:\\a\\Dawn\\third_party\\abseil-cpp\\absl\\base\\config.h'), '/d:/a/dawn/third_party/abseil-cpp/absl/base/config.h');
    assert.equal(toPosix('D:/a/dawn/out/windows-x64'), '/d:/a/dawn/out/windows-x64');
    assert.equal(toPosix('/w/dawn/Src/x.h'), '/w/dawn/Src/x.h');
  });

  it('maps MSVC deps, relative or with a differently cased drive, to the checkout', () => {
    const text = [
      'src\\dawn\\native\\CMakeFiles\\x.dir\\Device.cpp.obj: #deps 2, deps mtime 1 (VALID)',
      '    ..\\..\\third_party\\abseil-cpp\\absl\\base\\config.h',
      '    d:\\A\\dawn\\third_party\\khronos\\EGL\\egl.h',
      '    C:\\Program Files\\Windows Kits\\10\\Include\\d3d12.h',
      '',
    ].join('\n');
    const winRoot = toPosix('D:\\a\\dawn');
    const paths = depsPaths(text, toPosix('D:\\a\\dawn\\out\\windows-x64'));
    assert.deepEqual(paths, [
      '/d:/a/dawn/third_party/abseil-cpp/absl/base/config.h',
      '/d:/a/dawn/third_party/khronos/egl/egl.h',
      '/c:/program files/windows kits/10/include/d3d12.h',
    ]);
    const { found, unlicensed } = coveringLicenses(paths, winRoot, listDir);
    assert.deepEqual([...found.keys()], ['third_party/abseil-cpp', 'third_party/khronos']);
    assert.deepEqual(unlicensed, []);
  });
});
