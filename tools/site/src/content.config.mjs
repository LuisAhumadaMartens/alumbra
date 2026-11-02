import { docsSchema, i18nSchema } from '@astrojs/starlight/schema';
import { defineCollection } from 'astro:content';
import { glob } from 'astro/loaders';
import { pathToFileURL } from 'node:url';
import { docsRoot, excludedDocsDirectories, excludedDocsFiles, pageId, pageTitle } from './docs.mjs';
import { siteLanguage } from './site.mjs';

const repositoryDocs = glob({
  base: pathToFileURL(`${docsRoot}/`),
  pattern: [
    '**/*.md',
    ...excludedDocsDirectories.map((directory) => `!${directory}/**`),
    ...excludedDocsFiles.map((file) => `!${file}`),
  ],
  generateId: ({ entry }) => pageId(entry),
});

const docsLoader = {
  name: 'alumbra-docs-loader',
  load: (context) =>
    repositoryDocs.load({
      ...context,
      parseData: (entry) => context.parseData({ ...entry, data: { title: pageTitle(entry.filePath), ...entry.data } }),
    }),
};

export const collections = {
  docs: defineCollection({ loader: docsLoader, schema: docsSchema() }),
  i18n: defineCollection({ loader: () => [{ id: siteLanguage }], schema: i18nSchema() }),
};
