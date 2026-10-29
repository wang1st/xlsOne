# xlsOne international website

English public website for `https://xlsone.com`. This directory is separate from the domestic `site/` tree. It has no build step and contains no server credentials.

## Preview and checks

```sh
python3 -m http.server 4174 --bind 127.0.0.1 --directory international-site
node --check international-site/main.js
node --test international-site/checks/*.test.mjs
```

## Deployment

Publish the static contents of this directory to the **existing** xlsone.com Cloudflare Pages project. Preserve its domain, DNS, mail routes and any other attached services. Exclude `README.md` and `checks/` from the upload. `_headers` and `_redirects` are Cloudflare Pages configuration and must be included. This website does not replace the separate international activation Worker at `api.xlsone.com`.

The current configuration offers five public downloads from the GitHub release `international-v1.1.5`; their SHA-256 checksums and byte sizes match the verified CI artifact record. `config.js` and `api/version.json` use the same direct release URLs. Windows downloads are unsigned; the macOS universal application targets macOS 12+ on Apple Silicon and Intel and is not Apple-notarized. The owner-confirmed Paddle prices are US$4.99/month and US$39.99/year. Paid checkout remains disabled pending domain approval and the remaining payment/fulfillment checks. Do not represent the site as ready for purchase until those gates have passed.

## Release gates

1. Verify the international installer uses `https://api.xlsone.com`, trusts the current signing public key, offers the expected trial, has working English UI and can activate a subscription license. Verify its advertised system requirements. Upload the actual tested package to the download host.
2. Add each verified release to `config.js`:

```js
{
  platform: 'windows',
  label: 'Windows installer',
  version: 'ACTUAL_RELEASE_VERSION',
  requirements: 'ACTUAL_VERIFIED_REQUIREMENTS',
  format: 'MSI',
  url: 'https://YOUR_DOWNLOAD_HOST/ACTUAL_PACKAGE.msi',
  sha256: 'ACTUAL_64_CHARACTER_SHA256',
  verified: true
}
```

3. Obtain the owner's chosen price, create the xlsOne prices in Paddle Live, and fill `billing.monthly` and `billing.yearly` with the actual `priceId` and matching `displayPrice` (for example `US$4.99`). Do not copy Paprio price IDs.
4. Have `xlsone.com` approved in Paddle Website approval. Keep the policy and support pages accessible. Use `https://xlsone.com/checkout/` as the payment page for xlsOne transactions; do not overwrite account-wide settings that would break another product.
5. Deploy and test `/api/billing/session`, `/api/billing/claim`, and `/api/webhook/paddle`. Set the actual price IDs and webhook secret only in the Worker environment; never add a webhook secret or API key to this directory. Verify webhook signatures, order fulfillment, renewal expiry and cancellation behavior. Verify CORS permits `https://xlsone.com`.
6. Test the public site, downloads, payment failure/retry, key claim, desktop activation and Paddle portal before setting `billing.enabled` to `true`. The public `live_` client token is safe for frontend use; it is not an API key.

## Checkout contract

- `POST https://api.xlsone.com/api/billing/session` with `{ "plan": "monthly" | "yearly" }` returns `{ session_id, claim_token, price_id }`. The browser verifies `price_id` matches its selected displayed plan.
- Paddle receives `customData: { xlsone_session_id: session_id }`. The claim token never appears in a URL or Paddle metadata. It stays in the original tab's `sessionStorage` so a reload can resume key retrieval.
- Paddle.js `checkout.completed` triggers `POST /api/billing/claim`, with `Authorization: Bearer <claim_token>` and `{ session_id }`.
- Expected claim results: `202 {status:'pending'}`; `200 {status:'fulfilled',activation_key,expires_at,subscription_status}`; `401/404` unauthorized/not found; `410` expired. Do not treat a checkout browser callback as proof of entitlement. Only the signed server webhook can fulfill a purchase.
- SDK preparation failures before checkout is invoked clear the uncharged local session and allow a new attempt; failed SDK promises are not cached. Once checkout is invoked, recovery credentials are retained to avoid a duplicate payment. Claim polling is bounded. A pending or interrupted purchase displays a status check action and warns against paying again. Credentials are not cached in persistent local storage.
- `?_ptxn=txn_...` loads Paddle.js for an existing transaction when live checkout is enabled. A customer who lost the original purchase tab may need support to retrieve the license.
- The Paddle customer portal URL is shared by the seller's account. Paddle performs email authentication before showing account data. Its link works independently of this site's checkout enablement.

## Desktop subscription renewal

Current direct desktop licenses are signed locally verifiable files. After a renewal, users must re-enter the same activation key in the app to obtain the updated signed expiry. The website and support pages disclose this behavior. Cancellation stops future renewal; a paid period remains usable until its end unless refunded/reversed.

## Policies

The support address was taken from existing project deployment documentation. Verify it can still receive mail before public release. The policy text documents actual local file processing, online licensing and Paddle billing. Review the seller identity, policy wording and actual release behavior before enabling sales.
