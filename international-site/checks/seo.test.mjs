import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { createHash } from 'node:crypto';
import vm from 'node:vm';

const root = resolve(import.meta.dirname, '..');
const read = path => readFileSync(join(root, path), 'utf8');
const walk = path => readdirSync(path, { withFileTypes: true }).flatMap(entry =>
  entry.isDirectory() ? (entry.name === 'checks' ? [] : walk(join(path, entry.name))) : [join(path, entry.name)]);
const htmlFiles = walk(root).filter(path => path.endsWith('.html') && !/\/google[a-f0-9]+\.html$/.test(path));
const canonical = html => html.match(/<link rel="canonical" href="([^"]+)"/)?.[1];
const schemas = html => [...html.matchAll(/<script type="application\/ld\+json">([\s\S]*?)<\/script>/g)].map(match => match[1]);
const context = { window: {} };
vm.runInNewContext(read('config.js'), context);
const config = context.window.XLSONE_CONFIG;

test('search engines receive accurate prices and all verified downloads without JavaScript', () => {
  const home = read('index.html');
  assert.match(home, /<title>Excel Consolidation/);
  assert.match(home, /<h1>Consolidate Excel/);
  const pricing = read('pricing/index.html');
  assert.ok(pricing.includes(config.billing.monthly.displayPrice));
  assert.ok(pricing.includes(config.billing.yearly.displayPrice));
  assert.doesNotMatch(pricing, /Pricing to be announced|Checkout is being prepared|when checkout opens/);
  const downloads = read('download/index.html');
  for (const item of config.downloads.filter(item => item.verified)) {
    assert.ok(downloads.includes(`href="${item.url}"`), item.label);
    assert.ok(downloads.includes(item.sha256), `${item.label} checksum`);
  }
  assert.doesNotMatch(downloads, /downloads are being prepared|no verified download/);
});

test('every page has unique metadata, one H1 and a canonical on the production domain', () => {
  const titles = new Set();
  const descriptions = new Set();
  for (const path of htmlFiles) {
    const html = readFileSync(path, 'utf8');
    const title = html.match(/<title>([^<]+)<\/title>/)?.[1];
    const description = html.match(/<meta name="description" content="([^"]+)"/)?.[1];
    assert.ok(title && !titles.has(title), path);
    assert.ok(description && !descriptions.has(description), path);
    titles.add(title); descriptions.add(description);
    assert.equal([...html.matchAll(/<h1(?:\s[^>]*)?>/g)].length, 1, path);
    assert.ok(canonical(html)?.startsWith('https://xlsone.com/'), path);
    assert.ok(html.includes('https://xlsone.com/social-preview.png'), path);
  }
});

test('sitemap includes public landing pages and guides, and omits checkout and account utilities', () => {
  const urls = [...read('sitemap.xml').matchAll(/<loc>([^<]+)<\/loc>/g)].map(match => match[1]);
  assert.equal(urls.length, new Set(urls).size);
  for (const url of urls) {
    const path = new URL(url).pathname;
    assert.doesNotMatch(path, /checkout|account|404|checks/);
    const html = read(path === '/' ? 'index.html' : `${path.slice(1)}index.html`);
    assert.equal(canonical(html), url);
    assert.doesNotMatch(html, /content="noindex/);
  }
  assert.ok(urls.includes('https://xlsone.com/guides/'));
  assert.ok(urls.length >= 10);
  for (const path of ['account/index.html', 'checkout/index.html', '404.html']) {
    assert.match(read(path), /<meta name="robots" content="noindex, follow">/);
  }
  assert.doesNotMatch(read('robots.txt'), /Disallow: \/checkout/);
});

test('structured data parses, matches published prices, and is authorized by the content security policy', () => {
  const headers = read('_headers');
  const homeSchemas = schemas(read('index.html'));
  assert.ok(homeSchemas.length);
  for (const path of htmlFiles) {
    for (const json of schemas(readFileSync(path, 'utf8'))) {
      const data = JSON.parse(json);
      assert.equal(data['@context'], 'https://schema.org');
      assert.ok(headers.includes(`'sha256-${createHash('sha256').update(json).digest('base64')}'`), path);
      assert.doesNotMatch(json, /aggregateRating|reviewRating/);
    }
  }
  const app = homeSchemas.map(JSON.parse).flatMap(data => data['@graph'] || [data]).find(data => data['@type'] === 'SoftwareApplication');
  assert.ok(app);
  assert.equal(app.offers.price, '4.99');
  assert.equal(app.offers.priceCurrency, 'USD');
  const png = readFileSync(join(root, 'social-preview.png'));
  assert.equal(png.readUInt32BE(16), 1200);
  assert.equal(png.readUInt32BE(20), 630);
});

test('internal page links resolve to real files and every new guide is linked from another page', () => {
  const linked = new Set();
  for (const path of htmlFiles) {
    const html = readFileSync(path, 'utf8');
    for (const match of html.matchAll(/href="(\/[^"#?]*)(?:[?#][^"]*)?"/g)) {
      const url = match[1];
      if (url.startsWith('//')) continue;
      const file = join(root, url.slice(1), url.endsWith('/') ? 'index.html' : '');
      assert.ok(existsSync(file), `${path}: ${url}`);
      linked.add(url);
    }
  }
  for (const url of ['/guides/', '/guides/combine-excel-files/', '/guides/sum-matching-cells/']) assert.ok(linked.has(url));
});
