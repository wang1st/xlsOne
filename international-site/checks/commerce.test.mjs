import test from 'node:test';
import assert from 'node:assert/strict';
import { billingReady, verifiedDownloads, selectedPlan } from '../commerce.js';
const valid = { enabled:true, clientToken:'live_abc123', monthly:{priceId:'pri_01abc',displayPrice:'$4.99'},yearly:{priceId:'pri_01def',displayPrice:'$39.99'} };
test('checkout remains closed until explicitly enabled with all live settings',()=>{
 assert.equal(billingReady(valid),true);
 for(const config of [{...valid,enabled:false},{...valid,clientToken:'test_abc'},{...valid,monthly:{priceId:'',displayPrice:'$4.99'}},{...valid,yearly:{priceId:'pri_01def',displayPrice:''}}]) assert.equal(billingReady(config),false);
});
test('only verified HTTPS downloads with SHA-256 are offered',()=>{
 const good={platform:'windows',label:'Windows installer',url:'https://downloads.xlsone.com/xlsone.msi',version:'1.1.5',sha256:'a'.repeat(64),verified:true};
 assert.deepEqual(verifiedDownloads([good,{...good,verified:false},{...good,url:'javascript:alert(1)'},{...good,sha256:''}]),[good]);
});
test('checkout plan accepts only configured periods',()=>{
 assert.equal(selectedPlan('yearly'),'yearly');
 assert.equal(selectedPlan('anything'),'monthly');
 assert.equal(selectedPlan(null),'monthly');
});
