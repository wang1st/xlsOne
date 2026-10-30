import test from 'node:test';
import assert from 'node:assert/strict';

const key = 'AB12-CD34-EF56-GH78';
const fingerprint = 'a'.repeat(64);
const license = { key_id:key, device_hash:fingerprint, plan:'personal_yearly', issued_at:1793230270, expires_at:1795908670, signature:'a'.repeat(86) };
let sequence = 0;
async function withForm(fetchImpl, check) {
  const elements = new Map();
  for (const id of ['offline-form','offline-key','offline-device','offline-name','offline-submit','offline-status']) {
    elements.set(id,{value:'',disabled:false,textContent:'',listeners:{},setAttribute(){},addEventListener(event,fn){this.listeners[event]=fn;}});
  }
  elements.get('offline-key').value=key.toLowerCase();
  elements.get('offline-device').value=fingerprint.toUpperCase();
  elements.get('offline-name').value='Reporting laptop';
  const calls=[];
  const downloaded=[];
  const originals=Object.fromEntries(['window','document','fetch','sessionStorage'].map(name=>[name,globalThis[name]]));
  const localStorageDescriptor=Object.getOwnPropertyDescriptor(globalThis,'localStorage');
  globalThis.window={XLSONE_CONFIG:{apiBase:'https://api.xlsone.com'}};
  globalThis.document={
    getElementById:id=>elements.get(id)||null,
    querySelectorAll:()=>[],
    createElement:()=>({click(){downloaded.push({href:this.href,download:this.download});},remove(){}}),
    body:{append(){}}
  };
  const forbiddenStorage={getItem(){throw new Error('Do not persist offline credentials');},setItem(){throw new Error('Do not persist offline credentials');}};
  globalThis.sessionStorage=forbiddenStorage;
  Object.defineProperty(globalThis,'localStorage',{value:forbiddenStorage,configurable:true,writable:true});
  globalThis.fetch=async (url,options)=>{calls.push({url,options});return fetchImpl(url,options,calls.length);};
  try {
    await import(`../main.js?offline-activation-test-${++sequence}`);
    const submit=()=>elements.get('offline-form').listeners.submit({preventDefault(){}});
    await check({elements,calls,downloaded,submit});
  } finally {
    if(localStorageDescriptor)Object.defineProperty(globalThis,'localStorage',localStorageDescriptor);
    else delete globalThis.localStorage;
    for(const [name,value] of Object.entries(originals)) {
      if(value===undefined)delete globalThis[name];else globalThis[name]=value;
    }
  }
}

test('offline activation rejects invalid key and fingerprint without sending a request',async()=>{
  await withForm(()=>new Response('{}'),async({elements,calls,submit})=>{
    elements.get('offline-key').value='not a key';
    await submit();
    assert.equal(calls.length,0);
    assert.match(elements.get('offline-status').textContent,/activation key/i);
    elements.get('offline-key').value=key;
    elements.get('offline-device').value='bad device';
    await submit();
    assert.equal(calls.length,0);
    assert.match(elements.get('offline-status').textContent,/64/i);
    assert.equal(elements.get('offline-submit').disabled,false);
  });
});

test('offline activation uses POST body only and recovers after an API error',async()=>{
  await withForm((url,options,count)=>count===1
    ?new Response(JSON.stringify({error:'RATE_LIMITED',message:'<script>unsafe</script>'}),{status:429})
    :new Response(JSON.stringify(license),{headers:{'Content-Type':'application/json'}}),async({elements,calls,downloaded,submit})=>{
    await submit();
    assert.equal(elements.get('offline-submit').disabled,false);
    assert.match(elements.get('offline-status').textContent,/try again/i);
    assert.doesNotMatch(elements.get('offline-status').textContent,/<script>/);
    await submit();
    assert.equal(calls.length,2);
    for(const call of calls){
      assert.equal(call.url,'https://api.xlsone.com/api/license/download');
      assert.equal(call.options.method,'POST');
      assert.equal(call.options.credentials,'omit');
      assert.deepEqual(JSON.parse(call.options.body),{key,device_hash:fingerprint,device_name:'Reporting laptop'});
      assert.equal(call.url.includes(key),false);
    }
    assert.equal(downloaded.length,1);
    assert.equal(downloaded[0].download,'xlsone.license');
    assert.match(downloaded[0].href,/^blob:/);
    assert.equal(elements.get('offline-submit').disabled,false);
  });
});

test('offline activation rejects oversized responses and allows retry',async()=>{
  await withForm(()=>new Response('x'.repeat(17000)),async({elements,downloaded,submit})=>{
    await submit();
    assert.equal(downloaded.length,0);
    assert.equal(elements.get('offline-submit').disabled,false);
    assert.match(elements.get('offline-status').textContent,/try again|support/i);
  });
});

test('offline activation prevents duplicate requests while a request is pending',async()=>{
  let finish;
  const pending=new Promise(resolve=>{finish=resolve;});
  await withForm(()=>pending,async({elements,calls,downloaded,submit})=>{
    const first=submit();
    await Promise.resolve();
    await submit();
    assert.equal(calls.length,1);
    assert.equal(elements.get('offline-submit').disabled,true);
    finish(new Response(JSON.stringify(license)));
    await first;
    assert.equal(downloaded.length,1);
    assert.equal(elements.get('offline-submit').disabled,false);
  });
});
