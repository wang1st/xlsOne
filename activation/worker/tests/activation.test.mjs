import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { DatabaseSync } from 'node:sqlite'
import { test } from 'node:test'
import { ed25519 } from '@noble/curves/ed25519'
import app from '../.wrangler/test/worker.mjs'

const legacySeed = '01'.repeat(32)
const paddleSeed = '02'.repeat(32)

function signedBy(license, seed) {
  const { signature, ...payload } = license
  return ed25519.verify(Buffer.from(signature, 'base64url'),
    new TextEncoder().encode(JSON.stringify(payload)),
    ed25519.getPublicKey(Buffer.from(seed, 'hex')))
}

class Statement {
  constructor(db, sql, args = []) { Object.assign(this, { db, sql, args }) }
  bind(...args) { return new Statement(this.db, this.sql, args) }
  async first() { return this.db.prepare(this.sql).get(...this.args) ?? null }
  async run() { return this.db.prepare(this.sql).run(...this.args) }
}

function setup(status = 'exhausted', revoked = 0) {
  const db = new DatabaseSync(':memory:')
  db.exec(readFileSync(new URL('../schema.sql', import.meta.url), 'utf8'))
  const key = 'ABCD-EFGH-JKLM-NPQR'
  const expiry = new Date(Date.now() + 30 * 86400000).toISOString()
  db.prepare(`INSERT INTO windows_keys
    (key_id,plan,status,max_activations,activation_count,duration_days,device_hash,expires_at)
    VALUES (?,'personal_yearly',?,3,3,30,?,?)`).run(key, status, 'a'.repeat(64), expiry)
  for (const device of ['a', 'b', 'c']) {
    db.prepare('INSERT INTO windows_devices(device_id,key_id,revoked) VALUES (?,?,?)')
      .run(device.repeat(64), key, revoked)
  }
  return {
    db, key, expiry,
    env: { DB: { prepare(sql) { return new Statement(db, sql) } },
      ED25519_PRIVATE_KEY: legacySeed },
  }
}

async function activate(state, device, ip) {
  return app.request('/api/activate/windows', {
    method: 'POST', headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': ip },
    body: JSON.stringify({ key: state.key, device_hash: device.repeat(64), device_name: 'Test device' }),
  }, state.env)
}

test('existing three devices can refresh a renewed subscription without consuming activations', async () => {
  const state = setup()
  for (const device of ['a', 'b', 'c', 'a']) {
    const response = await activate(state, device, `renew-${device}`)
    assert.equal(response.status, 200, await response.clone().text())
    const body = await response.json()
    assert.equal(body.activation_count, 3)
    assert.equal(body.license.expires_at, Math.floor(Date.parse(state.expiry) / 1000))
  }
  assert.equal(state.db.prepare('SELECT activation_count FROM windows_keys').get().activation_count, 3)
  state.db.close()
})

test('a fourth device and revoked subscription cannot obtain a refreshed license', async () => {
  const state = setup()
  assert.equal((await activate(state, 'd', 'new-device')).status, 403)
  state.db.prepare("UPDATE windows_keys SET status='revoked'").run()
  assert.equal((await activate(state, 'a', 'revoked-key')).status, 403)
  state.db.close()
})

test('revoked device is denied even when it is the most recently used device', async () => {
  const state = setup('activated', 1)
  assert.equal((await activate(state, 'a', 'revoked-device')).status, 403)
  state.db.close()
})

test('legacy keys keep the original signing key when the Paddle signing key is configured', async () => {
  const state = setup()
  state.env.ED25519_PADDLE_PRIVATE_KEY = paddleSeed
  const response = await activate(state, 'a', 'legacy-signing')
  assert.equal(response.status, 200)
  const { license } = await response.json()
  assert.equal(signedBy(license, legacySeed), true)
  assert.equal(signedBy(license, paddleSeed), false)
  state.db.close()
})

test('Paddle keys use the new signing key and offline downloads select the same key', async () => {
  const state = setup()
  state.db.prepare("UPDATE windows_keys SET order_id='paddle:sub_test'").run()
  state.env.ED25519_PADDLE_PRIVATE_KEY = paddleSeed
  const response = await activate(state, 'a', 'paddle-signing')
  assert.equal(response.status, 200)
  const { license } = await response.json()
  assert.equal(signedBy(license, paddleSeed), true)
  assert.equal(signedBy(license, legacySeed), false)
  const download = await app.request('/api/license/download', {
    method: 'POST', headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': 'paddle-offline-signing' },
    body: JSON.stringify({ key: state.key, device_hash: 'a'.repeat(64) }),
  }, state.env)
  assert.equal(download.status, 200)
  const offlineLicense = await download.json()
  assert.equal(signedBy(offlineLicense, paddleSeed), true)
  assert.equal(signedBy(offlineLicense, legacySeed), false)
  state.db.close()
})

test('Paddle activation fails closed without a valid new signing key', async () => {
  for (const seed of [undefined, '', 'invalid']) {
    const state = setup()
    state.db.prepare("UPDATE windows_keys SET order_id='paddle:sub_test'").run()
    state.env.ED25519_PADDLE_PRIVATE_KEY = seed
    const response = await activate(state, 'a', `missing-paddle-signing-${seed}`)
    assert.equal(response.status, 500)
    const body = await response.json()
    assert.equal(body.license, undefined)
    assert.equal(state.db.prepare('SELECT activation_count FROM windows_keys').get().activation_count, 3)
    state.db.close()
  }
})

test('new trials prefer the Paddle signing key while unconfigured trial deployments retain legacy fallback', async () => {
  for (const paddleConfigured of [false, true]) {
    const state = setup()
    if (paddleConfigured) state.env.ED25519_PADDLE_PRIVATE_KEY = paddleSeed
    const response = await app.request('/api/trial/windows', {
      method: 'POST', headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': `trial-signing-${paddleConfigured}` },
      body: JSON.stringify({ device_hash: 'e'.repeat(64), device_name: 'Test trial device' }),
    }, state.env)
    assert.equal(response.status, 200)
    const { license } = await response.json()
    assert.equal(signedBy(license, paddleConfigured ? paddleSeed : legacySeed), true)
    assert.equal(signedBy(license, paddleConfigured ? legacySeed : paddleSeed), false)
    state.db.close()
  }
})

test('configured invalid trial key fails closed and legacy activation never uses the new key as fallback', async () => {
  const state = setup()
  state.env.ED25519_PADDLE_PRIVATE_KEY = 'invalid'
  const trial = await app.request('/api/trial/windows', {
    method: 'POST', headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': 'invalid-trial-signing' },
    body: JSON.stringify({ device_hash: 'f'.repeat(64) }),
  }, state.env)
  assert.equal(trial.status, 500)
  assert.equal(state.db.prepare('SELECT count(*) AS n FROM windows_trials').get().n, 0)
  state.env.ED25519_PADDLE_PRIVATE_KEY = paddleSeed
  delete state.env.ED25519_PRIVATE_KEY
  assert.equal((await activate(state, 'a', 'missing-legacy-signing')).status, 500)
  state.db.close()
})
