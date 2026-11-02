import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { join, posix, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

export const repositoryRoot = fileURLToPath(new URL('../../../', import.meta.url));
export const docsRoot = join(repositoryRoot, 'docs');
export const excludedDocsDirectories = ['api'];
export const excludedDocsFiles = ['adr/0000-template.md'];

const sectionLabels = new Map([
  ['', 'Overview'],
  ['architecture', 'Architecture'],
  ['adr', 'Decisions'],
  ['research', 'Research'],
  ['reference', 'Reference'],
]);
const collapsedSections = new Set(['adr', 'research']);

export function toPosix(path) {
  return path.split(sep).join(posix.sep);
}

export function isPagePath(docsPath) {
  const [first] = docsPath.split('/');
  return docsPath.endsWith('.md') && !excludedDocsDirectories.includes(first) && !excludedDocsFiles.includes(docsPath);
}

export function pageId(docsPath) {
  const segments = docsPath.replace(/\.md$/, '').split('/');
  if (segments.at(-1) === 'README') {
    segments.pop();
  }
  const id = segments.join('/').toLowerCase();
  return id === '' ? 'index' : id;
}

export function pageRoute(docsPath, base) {
  const id = pageId(docsPath);
  return id === 'index' ? base : `${base}${id}/`;
}

function plainText(markdown) {
  return markdown
    .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/`([^`]*)`/g, '$1')
    .replace(/(\*\*|__|\*|_)(\S(?:.*?\S)?)\1/g, '$2')
    .replace(/\\([\\`*_{}[\]()#+\-.!])/g, '$1')
    .trim();
}

export function pageTitle(filePath) {
  let fence = null;
  for (const line of readFileSync(filePath, 'utf8').split('\n')) {
    const marker = line.match(/^\s{0,3}(`{3,}|~{3,})/);
    if (marker) {
      if (fence === null) {
        fence = marker[1][0];
      } else if (marker[1][0] === fence) {
        fence = null;
      }
      continue;
    }
    const heading = fence === null && line.match(/^#\s+(.+?)\s*#*\s*$/);
    if (heading) {
      return plainText(heading[1]);
    }
  }
  throw new Error(`${toPosix(relative(repositoryRoot, filePath))} has no top-level heading to use as its title`);
}

function listPages(directory) {
  const pages = [];
  const directories = [];
  for (const name of readdirSync(join(docsRoot, directory)).sort()) {
    const docsPath = directory === '' ? name : `${directory}/${name}`;
    if (statSync(join(docsRoot, docsPath)).isDirectory()) {
      if (!(directory === '' && excludedDocsDirectories.includes(name))) {
        directories.push(docsPath);
      }
    } else if (isPagePath(docsPath)) {
      pages.push(docsPath);
    }
  }
  const isIndex = (docsPath) => Number(docsPath.split('/').at(-1) === 'README.md');
  pages.sort((a, b) => isIndex(b) - isIndex(a));
  return { pages, directories };
}

function sectionItems(directory) {
  const { pages, directories } = listPages(directory);
  return [
    ...pages.map((docsPath) => pageId(docsPath)),
    ...directories.map((child) => sectionGroup(child)),
  ];
}

function sectionGroup(directory) {
  const name = directory.split('/').at(-1);
  return {
    label: sectionLabels.get(directory) ?? name.charAt(0).toUpperCase() + name.slice(1).replaceAll('-', ' '),
    collapsed: collapsedSections.has(directory),
    items: sectionItems(directory),
  };
}

export function sidebar() {
  const { directories } = listPages('');
  const order = [...sectionLabels.keys()];
  const rank = (directory) => (order.includes(directory) ? order.indexOf(directory) : order.length);
  const overview = { label: sectionLabels.get(''), items: listPages('').pages.map((docsPath) => pageId(docsPath)) };
  const sections = directories.sort((a, b) => rank(a) - rank(b) || a.localeCompare(b)).map((directory) => sectionGroup(directory));
  return [overview, ...sections];
}

export function repositoryPathExists(repositoryPath) {
  return existsSync(join(repositoryRoot, repositoryPath));
}

export function repositoryPathIsDirectory(repositoryPath) {
  return statSync(join(repositoryRoot, repositoryPath)).isDirectory();
}
