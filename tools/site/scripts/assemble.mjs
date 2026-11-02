import { cpSync, existsSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { pathToFileURL } from 'node:url';
import { reservedSubtrees, sitePath, siteUrl } from '../src/site.mjs';

export function parseSubtree(argument) {
  const separator = argument.indexOf('=');
  if (separator <= 0 || separator === argument.length - 1) {
    throw new Error(`Subtree "${argument}" must have the form <route>=<directory>`);
  }
  const route = argument.slice(0, separator).replace(/^\/+|\/+$/g, '');
  if (!reservedSubtrees.includes(route)) {
    throw new Error(`Subtree route "${route}" is not reserved; use one of ${reservedSubtrees.join(', ')}`);
  }
  return { route, source: resolve(argument.slice(separator + 1)) };
}

export function assemble({ site, output, subtrees = [], base = sitePath }) {
  if (!existsSync(join(site, 'index.html'))) {
    throw new Error(`${site} does not contain a built site`);
  }
  if (existsSync(output) && readdirSync(output).length > 0) {
    throw new Error(`${output} must be empty or absent`);
  }
  const artefact = join(resolve(output), ...base.split('/').filter(Boolean));
  mkdirSync(artefact, { recursive: true });
  cpSync(site, artefact, { recursive: true });
  const routes = new Set();
  for (const { route, source } of subtrees) {
    if (routes.has(route)) {
      throw new Error(`Subtree route "${route}" is given more than once`);
    }
    routes.add(route);
    if (!existsSync(source) || !statSync(source).isDirectory() || !existsSync(join(source, 'index.html'))) {
      throw new Error(`Subtree "${route}" needs a directory with an index.html, not ${source}`);
    }
    const destination = join(artefact, ...route.split('/'));
    if (existsSync(destination)) {
      throw new Error(`Subtree "${route}" collides with ${destination} from the documentation build`);
    }
    mkdirSync(destination, { recursive: true });
    cpSync(source, destination, { recursive: true });
  }
  return { root: resolve(output), artefact };
}

function main() {
  const { values } = parseArgs({
    options: {
      site: { type: 'string', default: 'dist' },
      output: { type: 'string' },
      subtree: { type: 'string', multiple: true, default: [] },
    },
  });
  if (!values.output) {
    throw new Error('--output is required');
  }
  const { root, artefact } = assemble({
    site: resolve(values.site),
    output: values.output,
    subtrees: values.subtree.map(parseSubtree),
  });
  console.log(`root=${root}`);
  console.log(`artefact=${artefact}`);
  console.log(`base=${sitePath}`);
  console.log(`url=${siteUrl.href}`);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main();
}
