import test from 'node:test';
import assert from 'node:assert/strict';

test('configured prices are visible while unapproved checkout stays disabled', async () => {
  const elements = new Map();
  for(const id of ['checkout-status','billing-period','start-checkout','copy-key']) elements.set(id,{value:'',disabled:true,textContent:'',addEventListener(){}});
  const monthly={dataset:{price:'monthly'},textContent:'Pricing to be announced',classList:{remove(){}},append(node){this.period=node.textContent;}};
  const yearly={dataset:{price:'yearly'},textContent:'Pricing to be announced',classList:{remove(){}},append(node){this.period=node.textContent;}};
  const purchase={dataset:{purchase:'monthly'},href:'',textContent:''};
  let externalLoads=0;
  const originals=Object.fromEntries(['window','document','sessionStorage','location'].map(name=>[name,globalThis[name]]));
  globalThis.window={XLSONE_CONFIG:{billing:{enabled:false,clientToken:'live_public',monthly:{priceId:'pri_01m62za09xh2ky5chjggyy9h2n',displayPrice:'US$4.99'},yearly:{priceId:'pri_01m62zc4eksrh6b09h6d4z2px7',displayPrice:'US$39.99'}}}};
  globalThis.sessionStorage={getItem(){return null;}};
  globalThis.location={search:''};
  globalThis.document={
    getElementById:id=>elements.get(id)||null,
    querySelectorAll:selector=>selector==='[data-price]'?[monthly,yearly]:selector==='[data-purchase]'?[purchase]:[],
    createElement:()=>({textContent:''}),
    head:{append(){externalLoads++;}}
  };
  try {
    await import('../main.js?pricing-with-closed-checkout');
    assert.equal(monthly.textContent,'US$4.99');
    assert.equal(monthly.period,' / month');
    assert.equal(yearly.textContent,'US$39.99');
    assert.equal(yearly.period,' / year');
    assert.equal(elements.get('start-checkout').disabled,true);
    assert.equal(purchase.href,'/pricing/#availability');
    assert.equal(purchase.textContent,'View availability →');
    assert.match(elements.get('checkout-status').textContent,/No payment can be taken/i);
    assert.equal(externalLoads,0,'showing prices must not load Paddle or open checkout');
  } finally {
    for(const [name,value] of Object.entries(originals)) {if(value===undefined)delete globalThis[name];else globalThis[name]=value;}
  }
});
