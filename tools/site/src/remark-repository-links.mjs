import { dirname, relative, resolve } from 'node:path';
import {
  docsRoot,
  isPagePath,
  pageRoute,
  repositoryPathExists,
  repositoryPathIsDirectory,
  repositoryRoot,
  toPosix,
} from './docs.mjs';

const scheme = /^[a-z][a-z0-9+.-]*:/i;

function splitTarget(url) {
  const index = url.search(/[?#]/);
  return index === -1 ? [url, ''] : [url.slice(0, index), url.slice(index)];
}

function walk(node, visit) {
  visit(node);
  for (const child of node.children ?? []) {
    walk(child, visit);
  }
}

export function remarkRepositoryLinks({ base, repository, branch }) {
  const blobPrefix = `${repository}/blob/${branch}/`;
  const treePrefix = `${repository}/tree/${branch}/`;

  function repositoryUrl(repositoryPath, suffix) {
    const prefix = repositoryPath === '' || repositoryPathIsDirectory(repositoryPath) ? treePrefix : blobPrefix;
    return `${prefix}${repositoryPath.split('/').map(encodeURIComponent).join('/')}${suffix}`;
  }

  function siteUrl(repositoryPath, suffix) {
    if (repositoryPath !== 'docs' && !repositoryPath.startsWith('docs/')) {
      return null;
    }
    const docsPath = repositoryPath.slice('docs/'.length);
    if (!repositoryPathIsDirectory(repositoryPath)) {
      return isPagePath(docsPath) ? `${pageRoute(docsPath, base)}${suffix}` : null;
    }
    const index = docsPath === '' ? 'README.md' : `${docsPath}/README.md`;
    if (isPagePath(index) && repositoryPathExists(`docs/${index}`)) {
      return `${pageRoute(index, base)}${suffix}`;
    }
    return null;
  }

  function rewrite(repositoryPath, suffix) {
    if (!repositoryPathExists(repositoryPath)) {
      return { error: `${repositoryPath} does not exist` };
    }
    return { url: siteUrl(repositoryPath, suffix) ?? repositoryUrl(repositoryPath, suffix) };
  }

  function resolveLink(url, source) {
    for (const prefix of [blobPrefix, treePrefix]) {
      if (url.startsWith(prefix)) {
        const [path, suffix] = splitTarget(url.slice(prefix.length));
        const repositoryPath = decodeURIComponent(path).replace(/\/$/, '');
        return repositoryPath === 'docs' || repositoryPath.startsWith('docs/') ? rewrite(repositoryPath, suffix) : { url };
      }
    }
    if (url === '' || url.startsWith('#') || url.startsWith('/') || scheme.test(url)) {
      return { url };
    }
    const [path, suffix] = splitTarget(url);
    const target = resolve(dirname(source), decodeURIComponent(path));
    const repositoryPath = toPosix(relative(repositoryRoot, target));
    if (repositoryPath.startsWith('../') || repositoryPath === '..') {
      return { error: `${url} points outside the repository` };
    }
    return rewrite(repositoryPath, suffix);
  }

  return (tree, file) => {
    const source = file.path ?? file.history?.[0];
    if (!source || relative(docsRoot, source).startsWith('..')) {
      return;
    }
    const first = tree.children[0];
    if (first?.type === 'heading' && first.depth === 1) {
      tree.children.shift();
    }
    const errors = [];
    walk(tree, (node) => {
      if ((node.type === 'link' || node.type === 'definition') && typeof node.url === 'string') {
        const result = resolveLink(node.url, source);
        if (result.error) {
          errors.push(`${toPosix(relative(repositoryRoot, source))}:${node.position?.start.line ?? '?'}: ${result.error}`);
        } else {
          node.url = result.url;
        }
      }
    });
    if (errors.length > 0) {
      throw new Error(`Broken repository links:\n${errors.join('\n')}`);
    }
  };
}
