import { defineConfig } from 'astro/config';

// 站点发布地址。两者都可用环境变量覆盖，便于同一份源码在不同托管下构建：
//   SITE_URL  站点根的完整公开地址，必须已包含 SITE_BASE，且以 / 结尾
//   SITE_BASE 部署路径前缀，首尾都要带 /
const SITE_URL = process.env.SITE_URL ?? 'https://fengjutian.github.io/Folixa/';
const SITE_BASE = process.env.SITE_BASE ?? '/Folixa/';

export default defineConfig({
  site: SITE_URL,
  base: SITE_BASE,
  output: 'static',
  build: { format: 'directory' },
  // 站点产物单独输出：默认的 dist/ 是 C++ 打包目录（dist/*.zip），
  // astro build 会清空 outDir，直接用 dist/ 会把发布 zip 一起删掉。
  outDir: './site-dist',
});
