#!/usr/bin/env node
// Headers count too (ninja's deps log): header-only libraries are compiled in but are no sources.
// A compiled third_party file with no license above it fails the run instead of shipping unnoticed.
import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync } from 'node:fs';
import { posix } from 'node:path';
import { fileURLToPath } from 'node:url';

const LICENSE_FILE = /^(LICEN[CS]E|COPYING|NOTICE)([.-].*)?$/i;

export function licenseFilesIn(names) {
  return names.filter((n) => LICENSE_FILE.test(n)).sort();
}

export function coveringLicenses(paths, dawnRoot, listDir) {
  const found = new Map();
  const unlicensed = new Set();
  const memo = new Map();
  const licensesOf = (dir) => {
    if (!memo.has(dir)) memo.set(dir, licenseFilesIn(listDir(dir)));
    return memo.get(dir);
  };
  for (const path of paths) {
    const rel = posix.relative(dawnRoot, path);
    if (!rel.startsWith('third_party/')) continue;
    let dir = posix.dirname(rel);
    while (dir !== 'third_party' && licensesOf(dir).length === 0) dir = posix.dirname(dir);
    if (dir === 'third_party') unlicensed.add(rel.split('/').slice(0, 2).join('/'));
    else found.set(dir, licensesOf(dir));
  }
  return { found: new Map([...found].sort(([a], [b]) => a.localeCompare(b))), unlicensed: [...unlicensed].sort() };
}

export function depsPaths(text, buildDir) {
  const out = [];
  for (const line of text.split('\n')) {
    if (!/^\s+\S/.test(line)) continue;
    out.push(posix.resolve(buildDir, line.trim()));
  }
  return out;
}

function compiledPaths(buildDir) {
  const commands = JSON.parse(readFileSync(posix.join(buildDir, 'compile_commands.json'), 'utf8'));
  const sources = commands.map((c) => posix.resolve(c.directory, c.file));
  const deps = execFileSync('ninja', ['-C', buildDir, '-t', 'deps'], { encoding: 'utf8', maxBuffer: 1 << 30 });
  return [...sources, ...depsPaths(deps, buildDir)];
}

function main([dawnArg, buildArg, outArg]) {
  if (!outArg) {
    console.error('usage: collect-licenses.mjs <dawn checkout> <build dir> <out dir>');
    process.exit(2);
  }
  const dawnRoot = posix.resolve(dawnArg);
  const listDir = (rel) => {
    const abs = posix.join(dawnRoot, rel);
    return existsSync(abs) ? readdirSync(abs) : [];
  };
  const { found, unlicensed } = coveringLicenses(compiledPaths(posix.resolve(buildArg)), dawnRoot, listDir);
  if (unlicensed.length > 0) {
    console.error(`no license file covers compiled code in: ${unlicensed.join(', ')}`);
    process.exit(1);
  }
  mkdirSync(outArg, { recursive: true });
  copyFileSync(posix.join(dawnRoot, 'LICENSE'), posix.join(outArg, 'LICENSE'));
  for (const [dir, files] of found) {
    mkdirSync(posix.join(outArg, dir), { recursive: true });
    for (const f of files) copyFileSync(posix.join(dawnRoot, dir, f), posix.join(outArg, dir, f));
    console.log(`${dir}: ${files.join(', ')}`);
  }
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
