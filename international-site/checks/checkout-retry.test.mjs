import test from 'node:test';
import assert from 'node:assert/strict';

test('SDK loading failure clears an unopened session and the next checkout retries the SDK', async () => {
  const storage = new Map();
  const elements = new Map();
  for (const id of ['checkout-status','billing-period','start-checkout','retry-claim','license-result','checkout-controls','copy-key','activation-key','license-expiry']) {
    elements.set(id, { hidden: id === 'license-result', value:'monthly', disabled:true, textContent:'', listeners:{}, addEventListener(event, handler) { this.listeners[event] = handler; } });
  }
  elements.get('billing-period').options = [{value:'monthly'},{value:'yearly'}];
  let sdkAttempts = 0;
  let sessions = 0;
  const opened = [];
  const originals = Object.fromEntries(['window','document','sessionStorage','location','fetch'].map(key => [key,globalThis[key]]));
  globalThis.window = { XLSONE_CONFIG: { apiBase:'https://api.xlsone.com', billing:{enabled:true,clientToken:'live_example',monthly:{priceId:'pri_monthly',displayPrice:'US$4.99'},yearly:{priceId:'pri_yearly',displayPrice:'US$39.99'}}, downloads:[] } };
  globalThis.sessionStorage = {
    getItem: key => storage.get(key) ?? null,
    setItem: (key,value) => storage.set(key,value),
    removeItem: key => storage.delete(key)
  };
  globalThis.location = { search:'' };
  globalThis.document = {
    getElementById: id => elements.get(id) || null,
    querySelectorAll: () => [],
    createElement: () => ({remove(){}}),
    head: { append(script) {
      sdkAttempts++;
      queueMicrotask(() => {
        if (sdkAttempts === 1) script.onerror();
        else {
          window.Paddle = { Initialize() {}, Checkout:{open: data => opened.push(data)} };
          script.onload();
        }
      });
    } }
  };
  globalThis.fetch = async () => {
    sessions++;
    return new Response(JSON.stringify({session_id:`session_${sessions}`,claim_token:`claim_${sessions}`,price_id:'pri_monthly'}));
  };
  try {
    await import('../main.js?checkout-retry-regression');
    await elements.get('start-checkout').listeners.click();
    assert.equal(sdkAttempts,1);
    assert.equal(opened.length,0);
    assert.equal(storage.has('xlsone.purchase.session'),false,'an uncharged session must not trap the customer');
    assert.equal(elements.get('start-checkout').disabled,false,'the customer can retry before any checkout was invoked');
    await elements.get('start-checkout').listeners.click();
    assert.equal(sdkAttempts,2,'a failed SDK promise must not stay cached');
    assert.equal(opened.length,1);
    assert.equal(opened[0].customData.xlsone_session_id,`session_${sessions}`);
    assert.equal(JSON.parse(storage.get('xlsone.purchase.session')).checkout_started,true,'once checkout is invoked its recovery credential must remain');
  } finally {
    for (const [key,value] of Object.entries(originals)) {
      if (value === undefined) delete globalThis[key];
      else globalThis[key] = value;
    }
  }
});
