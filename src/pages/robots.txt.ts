import type { APIRoute } from 'astro';

// 与 astro.config.mjs 的 site 保持一致（含 base，且以 / 结尾）
const SITE = import.meta.env.SITE ?? 'https://fengjutian.github.io/Folixa/';

export const GET: APIRoute = () => {
  const body = `User-agent: *
Allow: /

Sitemap: ${new URL('sitemap.xml', SITE).href}
`;
  return new Response(body, {
    headers: { 'Content-Type': 'text/plain; charset=utf-8' }
  });
};
