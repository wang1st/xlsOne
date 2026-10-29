import { billingReady, verifiedDownloads, selectedPlan } from './commerce.js';

const config = window.XLSONE_CONFIG || {};
const ready = billingReady(config.billing);
const downloads = verifiedDownloads(config.downloads);
const byId = id => document.getElementById(id);

for (const link of document.querySelectorAll('[data-portal]')) {
  // The portal is public; Paddle authenticates customers by email before showing billing data.
  link.href = 'https://customer-portal.paddle.com/cpl_01jf9na30n6c0s1esnsk3f6cpb';
}

for (const link of document.querySelectorAll('[data-purchase]')) {
  const plan = selectedPlan(link.dataset.purchase);
  if (ready) {
    link.href = `/checkout/?plan=${plan}`;
    link.textContent = plan === 'yearly' ? 'Choose yearly →' : 'Choose monthly →';
  } else {
    link.href = '/pricing/#availability';
    link.textContent = 'View availability →';
  }
}

for (const price of document.querySelectorAll('[data-price]')) {
  const plan = selectedPlan(price.dataset.price);
  const publishedPrice = config.billing?.[plan];
  // Pricing can be published before the payment-domain and fulfillment checks are complete.
  if (/^pri_[a-zA-Z0-9]+$/.test(publishedPrice?.priceId || '')
    && typeof publishedPrice?.displayPrice === 'string' && publishedPrice.displayPrice.trim()) {
    price.textContent = publishedPrice.displayPrice;
    const period = document.createElement('small');
    period.textContent = plan === 'yearly' ? ' / year' : ' / month';
    price.append(period);
    price.classList.remove('pending');
  }
}

const availability = byId('availability');
if (availability && ready) {
  availability.textContent = 'Subscriptions renew automatically at the selected interval. Cancel future renewal at any time in the customer portal. Applicable taxes are shown before you pay.';
} else if (availability && config.billing?.monthly?.displayPrice && config.billing?.yearly?.displayPrice) {
  availability.textContent = 'Checkout is being prepared. The subscription prices shown above are confirmed. We are completing international release and payment setup. No payment can be taken through this page yet.';
}

const downloadList = byId('download-list');
if (downloadList && downloads.length) {
  byId('download-pending').hidden = true;
  for (const item of downloads) {
    const card = document.createElement('article');
    card.className = 'download-card';
    const title = document.createElement('h2');
    title.textContent = item.label;
    const version = document.createElement('p');
    version.textContent = `Version ${item.version}${item.requirements ? ' · ' + item.requirements : ''}`;
    const link = document.createElement('a');
    link.className = 'button';
    link.href = item.url;
    link.textContent = `Download${item.format ? ' ' + item.format : ''} ↓`;
    const caption = document.createElement('p');
    caption.textContent = 'SHA-256 checksum';
    const checksum = document.createElement('div');
    checksum.className = 'checksum';
    checksum.textContent = item.sha256;
    card.append(title, version, link, caption, checksum);
    downloadList.append(card);
  }
}

const demoButtons = document.querySelectorAll('[data-demo]');
for (const button of demoButtons) button.addEventListener('click', () => {
  for (const tab of demoButtons) tab.setAttribute('aria-pressed', String(tab === button));
  const source = button.dataset.demo === 'sources';
  byId('demo-explanation-title').textContent = source ? 'One number. Three clear sources.' : 'Three workbooks. One useful total.';
  byId('demo-explanation').textContent = source ? 'Revenue / May: North 12,400 + South 9,800 + West 7,600 = 29,800.' : 'Matching numeric cells are summed; source files stay unchanged.';
  byId('demo-file-label').textContent = source ? 'Cell C2 · Revenue' : 'Consolidated report';
});

function showStatus(message, kind = '') {
  const status = byId('checkout-status');
  if (!status) return;
  status.textContent = message;
  status.className = `status ${kind}`.trim();
}

const STORAGE_KEY = 'xlsone.purchase.session';
let activeSession = null;
let claiming = false;
let opening = false;
let paddlePromise;

function restoreSession() {
  try {
    const session = JSON.parse(sessionStorage.getItem(STORAGE_KEY) || 'null');
    if (session && typeof session.session_id === 'string' && typeof session.claim_token === 'string') {
      if (!/^txn_[a-z0-9]+$/i.test(session.transaction_id || '')) delete session.transaction_id;
      // This marker is written before SDK loading and changed before calling Checkout.open.
      // A failed pre-checkout preparation cannot have charged this session.
      if (session.checkout_started === false && !session.transaction_id) {
        sessionStorage.removeItem(STORAGE_KEY);
        return null;
      }
      return session;
    }
  } catch { /* A fresh checkout can still explain a storage restriction before payment. */ }
  return null;
}

async function request(path, options) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 12000);
  try {
    return await fetch(`${config.apiBase}${path}`, {
      ...options, credentials: 'omit', cache: 'no-store', signal: controller.signal
    });
  } finally { clearTimeout(timeout); }
}

async function claimPurchase() {
  if (!activeSession || claiming) return;
  claiming = true;
  byId('retry-claim').hidden = true;
  try {
    for (let attempt = 0; attempt < 15; attempt++) {
      const response = await request('/api/billing/claim', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${activeSession.claim_token}` },
        body: JSON.stringify({ session_id: activeSession.session_id })
      });
      const data = await response.json();
      if (response.ok && data.status === 'fulfilled' && /^[A-Z0-9]{4}(?:-[A-Z0-9]{4}){3}$/.test(data.activation_key)) {
        byId('activation-key').textContent = data.activation_key;
        byId('license-result').hidden = false;
        byId('checkout-controls').hidden = true;
        showStatus('Your payment is confirmed. Save your activation key below.', 'success');
        if (data.expires_at) {
          const expiry = new Date(data.expires_at);
          if (!Number.isNaN(expiry.getTime())) byId('license-expiry').textContent = `Current license period ends ${expiry.toLocaleDateString('en', { year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' })} (UTC).`;
        }
        return;
      }
      if (response.status === 410) {
        showStatus('This purchase session has expired. If you have paid, contact support with your Paddle receipt. Do not pay again.', 'error');
        return;
      }
      if (response.status === 401 || response.status === 404) {
        showStatus('This browser could not verify the purchase session. If you have paid, contact support with your Paddle receipt. Do not pay again.', 'error');
        return;
      }
      if (response.status !== 202) throw new Error('Claim unavailable');
      showStatus('Waiting for payment confirmation. Your activation key will appear here after Paddle confirms payment.');
      if (attempt < 14) await new Promise(resolve => setTimeout(resolve, 2000));
    }
    showStatus('Payment confirmation is still pending. Keep this page open, then choose “Check payment status”. If you have paid, do not start another purchase.');
    byId('retry-claim').hidden = false;
  } catch {
    showStatus('We could not check your payment right now. Keep this page open and try “Check payment status” again. If you have paid, do not pay again.', 'error');
    byId('retry-claim').hidden = false;
  } finally { claiming = false; }
}

function loadPaddle() {
  if (paddlePromise) return paddlePromise;
  paddlePromise = new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = 'https://cdn.paddle.com/paddle/v2/paddle.js';
    script.async = true;
    script.onerror = () => {
      script.remove();
      reject(new Error('Checkout unavailable'));
    };
    script.onload = () => {
      try {
        if (!window.Paddle) throw new Error('Checkout unavailable');
        window.Paddle.Initialize({
        token: config.billing.clientToken,
        checkout: { settings: { displayMode: 'overlay', locale: 'en', theme: 'light' } },
        eventCallback: event => {
          if (activeSession && /^txn_[a-z0-9]+$/i.test(event.data?.transaction_id || '')) {
            activeSession.transaction_id = event.data.transaction_id;
            try { sessionStorage.setItem(STORAGE_KEY, JSON.stringify(activeSession)); } catch { /* The in-memory session remains available. */ }
          }
          if (event.name === 'checkout.completed') {
            byId('start-checkout').disabled = true;
            claimPurchase();
          }
          if (event.name === 'checkout.closed' && activeSession?.transaction_id && byId('license-result').hidden && !claiming) {
            byId('start-checkout').disabled = false;
            byId('start-checkout').textContent = 'Resume Paddle checkout ↗';
            showStatus('Checkout was closed. Resume the same transaction below, or check its payment status if you have already paid.');
          }
          if (event.name === 'checkout.error') showStatus('Paddle could not open checkout. Please try again or contact support.', 'error');
        }
        });
        resolve(window.Paddle);
      } catch (error) {
        script.remove();
        reject(error);
      }
    };
    document.head.append(script);
  }).catch(error => {
    // Keep successful initialization cached; network/SDK failures must be retryable.
    paddlePromise = undefined;
    throw error;
  });
  return paddlePromise;
}

async function beginCheckout() {
  if (!ready || opening || claiming) return;
  if (activeSession) {
    if (activeSession.transaction_id) {
      opening = true;
      byId('start-checkout').disabled = true;
      try {
        const paddle = await loadPaddle();
        // Resume the existing transaction rather than creating a second charge.
        paddle.Checkout.open({ transactionId: activeSession.transaction_id });
        showStatus('Resuming your existing Paddle transaction. Do not start a second purchase.');
      } catch {
        showStatus('Your existing checkout could not reopen. Check payment status or contact support with your receipt.', 'error');
        byId('start-checkout').disabled = false;
      } finally { opening = false; }
      return;
    }
    showStatus('A purchase session is already open in this tab. Check its payment status before starting another purchase.');
    byId('retry-claim').hidden = false;
    return;
  }
  opening = true;
  const button = byId('start-checkout');
  button.disabled = true;
  try {
    const plan = selectedPlan(byId('billing-period').value);
    showStatus('Preparing your secure checkout…');
    // Confirm storage works before allowing a charge, so the claim credential survives a reload.
    sessionStorage.setItem(STORAGE_KEY, 'null');
    const response = await request('/api/billing/session', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ plan })
    });
    if (!response.ok) throw new Error('Session unavailable');
    const session = await response.json();
    if (!session.session_id || !session.claim_token || session.price_id !== config.billing[plan].priceId) throw new Error('Price unavailable');
    activeSession = { session_id: session.session_id, claim_token: session.claim_token, checkout_started: false };
    sessionStorage.setItem(STORAGE_KEY, JSON.stringify(activeSession));
    const paddle = await loadPaddle();
    // Persist the conservative recovery marker before allowing Paddle to create a transaction.
    sessionStorage.setItem(STORAGE_KEY, JSON.stringify({ ...activeSession, checkout_started: true }));
    activeSession.checkout_started = true;
    paddle.Checkout.open({ items: [{ priceId: session.price_id, quantity: 1 }], customData: { xlsone_session_id: session.session_id } });
    showStatus('Complete payment in the Paddle checkout, then return here for your activation key.');
    byId('retry-claim').hidden = false;
  } catch {
    const safeToRetry = !activeSession || (activeSession.checkout_started === false && !activeSession.transaction_id);
    if (safeToRetry) {
      activeSession = null;
      try { sessionStorage.removeItem(STORAGE_KEY); } catch { /* No checkout was opened; a storage restriction is reported below. */ }
      showStatus('Checkout could not be prepared. No checkout was opened. Check your connection, allow session storage in this browser, then try again.', 'error');
    } else {
      showStatus('Checkout could not be confirmed. Check payment status or contact support with your receipt. If a payment was made, do not pay again.', 'error');
      byId('retry-claim').hidden = false;
    }
  } finally {
    opening = false;
    button.disabled = Boolean(activeSession);
  }
}

if (byId('checkout-status')) {
  activeSession = restoreSession();
  const plan = selectedPlan(new URLSearchParams(location.search).get('plan'));
  byId('billing-period').value = plan;
  if (ready) {
    byId('start-checkout').disabled = false;
    byId('billing-period').disabled = false;
    for (const option of byId('billing-period').options) option.textContent = `${option.value === 'yearly' ? 'Yearly' : 'Monthly'} · ${config.billing[option.value].displayPrice} / ${option.value === 'yearly' ? 'year' : 'month'}`;
    showStatus('Choose a billing period to continue to secure checkout. Taxes, if applicable, are shown by Paddle before payment.');
    byId('start-checkout').addEventListener('click', beginCheckout);
    byId('retry-claim').addEventListener('click', claimPurchase);
    if (activeSession) {
      byId('start-checkout').disabled = true;
      if (activeSession.transaction_id) {
        byId('start-checkout').textContent = 'Resume Paddle checkout ↗';
        byId('start-checkout').disabled = false;
      }
      claimPurchase();
    }
    const transaction = new URLSearchParams(location.search).get('_ptxn');
    if (transaction && /^txn_[a-z0-9]+$/i.test(transaction)) {
      byId('start-checkout').disabled = true;
      loadPaddle().catch(() => showStatus('Paddle checkout could not load. Please try again later or contact support.', 'error'));
      if (!activeSession) showStatus('You are completing an existing Paddle transaction. Your activation key appears in the browser tab where you started the purchase. Contact support with your receipt if you no longer have that tab.');
    }
  } else {
    showStatus('Checkout is being prepared. We are completing release and payment setup. No payment can be taken from this page yet.');
  }
  byId('copy-key').addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(byId('activation-key').textContent);
      byId('copy-key').textContent = 'Copied ✓';
    } catch { byId('copy-key').textContent = 'Select and copy the key above'; }
  });
}

async function boundedResponseText(response, limit = 16384) {
  const advertisedLength = response.headers.get('Content-Length');
  if (advertisedLength !== null && (!/^\d+$/.test(advertisedLength) || Number(advertisedLength) > limit)) {
    await response.body?.cancel();
    throw new Error('Response too large');
  }
  if (!response.body) return '';
  const reader = response.body.getReader();
  const chunks = [];
  let length = 0;
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      length += value.byteLength;
      if (length > limit) {
        await reader.cancel();
        throw new Error('Response too large');
      }
      chunks.push(value);
    }
  } finally { reader.releaseLock(); }
  const bytes = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
  return new TextDecoder().decode(bytes);
}

const offlineForm = byId('offline-form');
if (offlineForm) {
  const button = byId('offline-submit');
  const status = byId('offline-status');
  let busy = false;
  const report = (message, kind = '') => {
    status.textContent = message;
    status.className = `status ${kind}`.trim();
  };
  button.disabled = false;
  offlineForm.addEventListener('submit', async event => {
    event.preventDefault();
    if (busy) return;
    const key = byId('offline-key').value.trim().toUpperCase();
    const deviceHash = byId('offline-device').value.trim().toLowerCase();
    const deviceName = byId('offline-name').value.trim();
    if (!/^[A-Z0-9]{4}(?:-[A-Z0-9]{4}){3}$/.test(key)) {
      report('Enter an activation key in XXXX-XXXX-XXXX-XXXX format.', 'error');
      return;
    }
    if (!/^[a-f0-9]{64}$/.test(deviceHash)) {
      report('Copy the full 64-character device fingerprint from the app’s license dialog.', 'error');
      return;
    }
    if (deviceName.length > 100) {
      report('Use a device name of 100 characters or fewer.', 'error');
      return;
    }
    busy = true;
    button.disabled = true;
    offlineForm.setAttribute('aria-busy', 'true');
    report('Creating your signed license file…');
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 20000);
    try {
      // Credentials travel only in this POST body, never in URLs or browser storage.
      const response = await fetch('https://api.xlsone.com/api/license/download', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        credentials: 'omit', cache: 'no-store', referrerPolicy: 'no-referrer',
        signal: controller.signal,
        body: JSON.stringify({ key, device_hash: deviceHash, device_name: deviceName })
      });
      const text = await boundedResponseText(response);
      const data = JSON.parse(text);
      if (!response.ok) {
        const messages = {
          KEY_NOT_FOUND: 'This activation key could not be found. Check the key or contact support.',
          KEY_REVOKED: 'This activation key is no longer active. Contact support for help.',
          SUBSCRIPTION_EXPIRED: 'This license period has expired. Check your subscription in the customer portal or contact support.',
          EXHAUSTED: 'This activation key has reached its device limit. Contact support for help.',
          RATE_LIMITED: 'Too many requests. Wait a moment, then try again.',
          INVALID_KEY: 'Check your activation key and try again.',
          INVALID_DEVICE: 'Check the device fingerprint copied from the app and try again.'
        };
        report(messages[data?.error] || 'The activation service could not create a license. Please try again or contact support.', 'error');
        return;
      }
      if (data?.key_id !== key || data?.device_hash !== deviceHash
        || typeof data.plan !== 'string' || !Number.isFinite(data.issued_at) || !Number.isFinite(data.expires_at)
        || !/^[A-Za-z0-9_-]{80,128}$/.test(data.signature || '')) throw new Error('Invalid license response');
      const downloadUrl = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
      const link = document.createElement('a');
      link.href = downloadUrl;
      link.download = 'xlsone.license';
      document.body.append(link);
      link.click();
      link.remove();
      setTimeout(() => URL.revokeObjectURL(downloadUrl), 1000);
      report('Your xlsone.license file is ready. Save it, transfer it to the matching computer, and import it through the app’s offline activation option.', 'success');
    } catch {
      report('We could not create a valid license file. Check your connection and try again, or contact support. No activation key has been saved in this browser.', 'error');
    } finally {
      clearTimeout(timeout);
      busy = false;
      button.disabled = false;
      offlineForm.setAttribute('aria-busy', 'false');
    }
  });
}
