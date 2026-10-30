# xlsOne international SEO — 2026-10-29

## Search intent and pages

These themes describe the product and page intent. They are not measured search-volume or ranking claims.

| URL | Primary intent |
| --- | --- |
| https://xlsone.com/ | Excel consolidation software; local consolidation of matching workbooks |
| https://xlsone.com/download/ | Download xlsOne for Windows, macOS or Linux |
| https://xlsone.com/pricing/ | xlsOne price, monthly and annual subscription terms |
| https://xlsone.com/guides/ | Practical Excel consolidation guides |
| https://xlsone.com/guides/combine-excel-files/ | Combine Excel workbooks with the same report template |
| https://xlsone.com/guides/sum-matching-cells/ | Sum corresponding cells across multiple Excel files |
| https://xlsone.com/support/ | File compatibility, license activation and billing help |

The guides distinguish adding matching cells from appending records or joining tables. They include template, currency, reporting-period and duplicate-input checks. They do not promise unsupported spreadsheet transformations.

## Implemented

- Descriptive page-specific titles and descriptions; one H1 per page; production canonical URLs and matching Open Graph URLs.
- True published prices and five verified installer links appear in the initial HTML response. JavaScript enhancement does not duplicate the download cards.
- A linked guide hub and two original worked guides; navigation and homepage links make them discoverable.
- A 1200 × 630 PNG social preview (illustrative sample data, not a product screenshot).
- WebSite, Organization, SoftwareApplication, WebPage, BreadcrumbList and Article JSON-LD where relevant. Subscription offers specify USD prices and billing duration. No fabricated rating or review markup.
- Exact content hashes authorize inline JSON-LD under the existing CSP. Recalculate these hashes when changing the JSON-LD.
- Ten public canonical pages in sitemap.xml. Checkout, account utilities and 404 pages use noindex. robots.txt permits fetching checkout so that the noindex directive can be seen.
- Google Search Console HTML verification file is preserved in the public root.

## Validation

- Fifteen frontend tests pass, including metadata, sitemap eligibility, internal link resolution, structured-data parsing and CSP hashes, static commerce facts, and download enhancement without duplicates.
- Independent review passed. Existing checkout preparation, retry and offline activation checks remain green.
- A 390-pixel viewport check confirms the homepage document fits its viewport; the guide layout is readable and wide tables scroll within their wrapper. This is a layout check, not a Core Web Vitals field measurement.
- Production release `995781ba` serves ten public sitemap pages. Google Search Console ownership for `https://xlsone.com/` was verified through the preserved HTML file. On October 30, 2026, sitemap submission returned Success and Google reported ten discovered pages. This is not an indexing or ranking claim.

## Follow-up measurement

Use Search Console's indexing, sitemap and performance reports to inspect actual queries, impressions, clicks and click-through rate after Google has collected data. A successful sitemap submission is not proof that every URL has been indexed or ranked. Do not claim an achieved ranking based on metadata changes alone.

When releasing a new app version, keep config.js, api/version.json, static download HTML and the software schema aligned. When changing prices, update both static pricing and software offers. The SEO checks detect drift against config.js.

## Primary references

- [Google Search Essentials](https://developers.google.com/search/docs/essentials)
- [Title links](https://developers.google.com/search/docs/appearance/title-link)
- [Canonical URL guidance](https://developers.google.com/search/docs/crawling-indexing/consolidate-duplicate-urls)
- [Robots meta and X-Robots-Tag](https://developers.google.com/search/docs/crawling-indexing/robots-meta-tag)
- [Software app structured data](https://developers.google.com/search/docs/appearance/structured-data/software-app)
- [Microsoft Power Query combine-files overview](https://learn.microsoft.com/en-us/power-query/combine-files-overview)
