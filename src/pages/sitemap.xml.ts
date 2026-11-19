import type { APIRoute } from 'astro';

// 与 astro.config.mjs 的 site 保持一致（含 base，且以 / 结尾）
const SITE = import.meta.env.SITE ?? 'https://fengjutian.github.io/Folixa/';

export const GET: APIRoute = () => {
  const body = `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
  <url>
    <loc>${new URL('.', SITE).href}</loc>
    <changefreq>weekly</changefreq>
    <priority>1.0</priority>
  </url>
</urlset>
`;
  return new Response(body, {
    headers: { 'Content-Type': 'application/xml; charset=utf-8' }
  });
};
