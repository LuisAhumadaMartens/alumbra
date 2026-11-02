import assert from 'node:assert/strict';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, test } from 'node:test';
import { reservedSubtrees } from '../src/site.mjs';
import { assemble, parseSubtree } from './assemble.mjs';

let work;

function page(directory, text) {
  mkdirSync(directory, { recursive: true });
  writeFileSync(join(directory, 'index.html'), text);
}

beforeEach(() => {
  work = mkdtempSync(join(tmpdir(), 'alumbra-assemble-'));
  page(join(work, 'site'), 'docs');
  page(join(work, 'site', 'adr'), 'decisions');
});

afterEach(() => {
  rmSync(work, { recursive: true, force: true });
});

test('places the documentation under the base path', () => {
  const { root, artefact } = assemble({ site: join(work, 'site'), output: join(work, 'out'), base: '/alumbra/' });
  assert.equal(root, join(work, 'out'));
  assert.equal(artefact, join(work, 'out', 'alumbra'));
  assert.equal(readFileSync(join(artefact, 'index.html'), 'utf8'), 'docs');
  assert.equal(readFileSync(join(artefact, 'adr', 'index.html'), 'utf8'), 'decisions');
});

test('a root base path makes the output the artefact', () => {
  const { artefact } = assemble({ site: join(work, 'site'), output: join(work, 'out'), base: '/' });
  assert.equal(artefact, join(work, 'out'));
});

test('adds every reserved subtree at its route', () => {
  const subtrees = reservedSubtrees.map((route) => {
    page(join(work, 'subtrees', route), route);
    return parseSubtree(`${route}=${join(work, 'subtrees', route)}`);
  });
  const { artefact } = assemble({ site: join(work, 'site'), output: join(work, 'out'), subtrees, base: '/alumbra/' });
  for (const route of reservedSubtrees) {
    assert.equal(readFileSync(join(artefact, ...route.split('/'), 'index.html'), 'utf8'), route);
  }
});

test('rejects routes that are not reserved or are malformed', () => {
  assert.throws(() => parseSubtree(`adr=${work}`), /not reserved/);
  assert.throws(() => parseSubtree('api/cpp'), /<route>=<directory>/);
  assert.throws(() => parseSubtree('=dir'), /<route>=<directory>/);
  assert.throws(() => parseSubtree('api/cpp='), /<route>=<directory>/);
  assert.equal(parseSubtree('/api/cpp/=x').route, 'api/cpp');
});

test('rejects subtrees without an index page, duplicates and collisions', () => {
  mkdirSync(join(work, 'empty'));
  page(join(work, 'demo'), 'demo');
  const site = join(work, 'site');
  assert.throws(
    () => assemble({ site, output: join(work, 'a'), subtrees: [parseSubtree(`demo=${join(work, 'empty')}`)] }),
    /needs a directory with an index.html/,
  );
  assert.throws(
    () => assemble({ site, output: join(work, 'b'), subtrees: [parseSubtree(`demo=${join(work, 'missing')}`)] }),
    /needs a directory with an index.html/,
  );
  const demo = parseSubtree(`demo=${join(work, 'demo')}`);
  assert.throws(() => assemble({ site, output: join(work, 'c'), subtrees: [demo, demo] }), /more than once/);
  page(join(site, 'demo'), 'docs page');
  assert.throws(() => assemble({ site, output: join(work, 'd'), subtrees: [demo] }), /collides/);
});

test('refuses a missing site and a non-empty output', () => {
  assert.throws(() => assemble({ site: join(work, 'missing'), output: join(work, 'out') }), /does not contain a built site/);
  page(join(work, 'used'), 'old');
  assert.throws(() => assemble({ site: join(work, 'site'), output: join(work, 'used') }), /must be empty or absent/);
  assert.ok(existsSync(join(work, 'used', 'index.html')));
});
