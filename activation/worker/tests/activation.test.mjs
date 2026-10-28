import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { DatabaseSync } from 'node:sqlite'
import { test } from 'node:test'
import app from '../.wrangler/test/worker.mjs'

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
      ED25519_PRIVATE_KEY: '01'.repeat(32) },
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
