import { unified } from '@astrojs/markdown-remark';
import starlight from '@astrojs/starlight';
import { defineConfig, passthroughImageService } from 'astro/config';
import rehypeMermaid from 'rehype-mermaid';
import { sidebar } from './src/docs.mjs';
import { remarkRepositoryLinks } from './src/remark-repository-links.mjs';
import { repository, repositoryBranch, siteLanguage, sitePath, siteUrl } from './src/site.mjs';

export default defineConfig({
  site: siteUrl.href,
  base: sitePath,
  trailingSlash: 'always',
  image: { service: passthroughImageService() },
  markdown: {
    processor: unified({
      remarkPlugins: [[remarkRepositoryLinks, { base: sitePath, repository, branch: repositoryBranch }]],
      rehypePlugins: [[rehypeMermaid, { strategy: 'inline-svg', colorScheme: 'light' }]],
    }),
  },
  integrations: [
    starlight({
      title: 'Alumbra',
      description: 'A portable media player engine with an HTMLMediaElement-shaped API on every platform.',
      favicon: '/favicon.svg',
      locales: { root: { label: 'English', lang: siteLanguage } },
      social: [{ icon: 'github', label: 'GitHub', href: repository }],
      sidebar: sidebar(),
      customCss: ['./src/styles/site.css'],
      disable404Route: true,
    }),
  ],
});
