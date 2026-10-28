import assert from 'node:assert/strict'
import { createHmac } from 'node:crypto'
import { readFileSync } from 'node:fs'
import { DatabaseSync } from 'node:sqlite'
import { test } from 'node:test'
import { handlePaddleRequest, verifyPaddleSignature } from '../src/paddle.ts'

const secret = 'test-notification-secret-for-unit-tests'
const monthly = 'pri_01aaaaaaaaaaaaaaaaaaaaaaaa'
const yearly = 'pri_01bbbbbbbbbbbbbbbbbbbbbbbb'
const subId = 'sub_01aaaaaaaaaaaaaaaaaaaaaaaa'
const base = Date.now()
const date = (offset = 0) => new Date(base + offset).toISOString()
const day = 86_400_000
let eventSequence = 0

class Statement {
  db: DatabaseSync
  sql: string
  args: unknown[]
  constructor(db: DatabaseSync, sql: string, args: unknown[] = []) {
    this.db = db; this.sql = sql; this.args = args
  }
  bind(...args: unknown[]) { return new Statement(this.db, this.sql, args) }
  async first() { return this.db.prepare(this.sql).get(...this.args) ?? null }
  runSync() {
    const result = this.db.prepare(this.sql).run(...this.args)
    return { success: true, meta: { changes: Number(result.changes) } }
  }
  async run() { return this.runSync() }
}

function environment() {
  const sqlite = new DatabaseSync(':memory:')
  sqlite.exec(readFileSync(new URL('../schema.sql', import.meta.url), 'utf8'))
  sqlite.exec(readFileSync(new URL('../migrations/0001_paddle.sql', import.meta.url), 'utf8'))
  const DB = {
    prepare(sql: string) { return new Statement(sqlite, sql) },
    async batch(statements: Statement[]) {
      sqlite.exec('BEGIN')
      try {
        const results = []
        for (const statement of statements) results.push(statement.runSync())
        sqlite.exec('COMMIT')
        return results
      } catch (error) { sqlite.exec('ROLLBACK'); throw error }
    },
  }
  return { DB, sqlite, PADDLE_WEBHOOK_SECRET: secret, PADDLE_MONTHLY_PRICE_ID: monthly, PADDLE_YEARLY_PRICE_ID: yearly }
}

function signature(raw: string, timestamp = Math.floor(Date.now() / 1000)) {
  return `ts=${timestamp};h1=${createHmac('sha256', secret).update(`${timestamp}:${raw}`).digest('hex')}`
}

async function session(env: ReturnType<typeof environment>, plan = 'monthly') {
  const response = await handlePaddleRequest(new Request('https://api.xlsone.com/api/billing/session', {
    method: 'POST', headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': '203.0.113.1' }, body: JSON.stringify({ plan }),
  }), env as never)
  assert.equal(response.status, 201)
  return await response.json() as { session_id: string; claim_token: string; price_id: string }
}

function transaction(sessionId: string, overrides: Record<string, unknown> = {}) {
  return {
    event_id: `evt_${String(++eventSequence).padStart(26, '0')}`,
    event_type: 'transaction.completed', occurred_at: date(),
    data: {
      id: 'txn_01aaaaaaaaaaaaaaaaaaaaaaaa', status: 'completed', origin: 'web',
      subscription_id: subId, customer_id: 'ctm_01aaaaaaaaaaaaaaaaaaaaaaaa',
      items: [{ price: { id: monthly }, quantity: 1 }],
      custom_data: { xlsone_session_id: sessionId },
      billing_period: { starts_at: date(), ends_at: date(30 * day) }, ...overrides,
    },
  }
}

function subscription(sessionId: string, status = 'active', offset = 1000) {
  return {
    event_id: `evt_${String(++eventSequence).padStart(26, '0')}`,
    event_type: `subscription.${status === 'active' ? 'updated' : status}`, occurred_at: date(offset),
    data: {
      id: subId, status, customer_id: 'ctm_01aaaaaaaaaaaaaaaaaaaaaaaa',
      items: [{ price: { id: monthly }, quantity: 1 }],
      custom_data: { xlsone_session_id: sessionId },
      current_billing_period: status === 'active' ? { starts_at: date(), ends_at: date(30 * day) } : null,
      scheduled_change: status === 'active' ? { action: 'cancel', effective_at: date(30 * day) } : null,
    },
  }
}

async function webhook(env: ReturnType<typeof environment>, event: ReturnType<typeof transaction> | ReturnType<typeof subscription>) {
  const raw = JSON.stringify(event)
  return handlePaddleRequest(new Request('https://api.xlsone.com/api/webhook/paddle', {
    method: 'POST', headers: { 'Paddle-Signature': signature(raw) }, body: raw,
  }), env as never)
}

async function claim(env: ReturnType<typeof environment>, purchase: Awaited<ReturnType<typeof session>>, token = purchase.claim_token) {
  return handlePaddleRequest(new Request(`https://api.xlsone.com/api/billing/claim?session_id=${purchase.session_id}`, {
    headers: { Authorization: `Bearer ${token}`, 'CF-Connecting-IP': '203.0.113.1' },
  }), env as never)
}

test('signature uses exact body and rejects expired, future and malformed signatures', async () => {
  const raw = '{"message":"a"}'
  assert.equal(await verifyPaddleSignature(raw, signature(raw), secret), true)
  assert.equal(await verifyPaddleSignature(raw + ' ', signature(raw), secret), false)
  assert.equal(await verifyPaddleSignature(raw, signature(raw, Math.floor(Date.now() / 1000) - 301), secret), false)
  assert.equal(await verifyPaddleSignature(raw, signature(raw, Math.floor(Date.now() / 1000) + 301), secret), false)
  assert.equal(await verifyPaddleSignature(raw, 'ts=1;ts=2;h1=bad', secret), false)
  assert.equal(await verifyPaddleSignature(raw, 'ts=NaN;h1=bad', secret), false)
})

test('session requires configured billing and stores only hash of the bearer claim token', async () => {
  const env = environment()
  const purchase = await session(env)
  assert.match(purchase.claim_token, /^[a-f0-9]{64}$/)
  assert.equal(purchase.price_id, monthly)
  const stored = env.sqlite.prepare('SELECT * FROM paddle_checkout_sessions').get()!
  assert.notEqual(stored.claim_hash, purchase.claim_token)
  assert.equal(JSON.stringify(stored).includes(purchase.claim_token), false)
  env.PADDLE_WEBHOOK_SECRET = ''
  const disabled = await handlePaddleRequest(new Request('https://api.xlsone.com/api/billing/session', { method: 'POST', body: '{"plan":"monthly"}' }), env as never)
  assert.equal(disabled.status, 503)
})

test('claim never authenticates with a transaction ID, email, or session ID alone', async () => {
  const env = environment()
  const purchase = await session(env)
  assert.equal((await claim(env, purchase, 'f'.repeat(64))).status, 404)
  assert.equal((await claim(env, purchase, '')).status, 401)
  assert.equal((await claim(env, purchase)).status, 202)
})

test('verified payment issues one compatible activation code with the paid period expiry', async () => {
  const env = environment()
  const purchase = await session(env)
  const event = transaction(purchase.session_id)
  assert.equal((await webhook(env, event)).status, 200)
  assert.equal((await webhook(env, event)).status, 200)
  const response = await claim(env, purchase)
  assert.equal(response.status, 200)
  const data = await response.json() as Record<string, unknown>
  assert.match(String(data.activation_key), /^[A-Z0-9]{4}(-[A-Z0-9]{4}){3}$/)
  assert.equal(data.expires_at, date(30 * day))
  const rows = env.sqlite.prepare('SELECT * FROM windows_keys').all()
  assert.equal(rows.length, 1)
  assert.equal(rows[0].plan, 'personal_yearly')
  assert.equal(rows[0].max_activations, 3)
  assert.notEqual(rows[0].expires_at, null)
})

test('unrelated products and non-charge transaction types never issue activation keys', async () => {
  const env = environment()
  const purchase = await session(env)
  assert.equal((await webhook(env, transaction(purchase.session_id, { items: [{ price: { id: 'pri_01papriopapriopapriopaprio' }, quantity: 1 }] }))).status, 200)
  assert.equal((await webhook(env, transaction(purchase.session_id, { origin: 'subscription_payment_method_change' }))).status, 200)
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 0)
})

test('missing billing period and mismatched session price fail closed', async () => {
  const env = environment()
  const purchase = await session(env)
  assert.equal((await webhook(env, transaction(purchase.session_id, { billing_period: null }))).status, 422)
  assert.equal((await webhook(env, transaction(purchase.session_id, { items: [{ price: { id: yearly }, quantity: 1 }] }))).status, 422)
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 0)
})

test('renewal extends paid expiry without issuing another key and older payments cannot shrink it', async () => {
  const env = environment()
  const purchase = await session(env)
  const initial = transaction(purchase.session_id)
  await webhook(env, initial)
  const keyBefore = env.sqlite.prepare('SELECT key_id FROM windows_keys').get()!.key_id
  const renewal = transaction(purchase.session_id, { id: 'txn_01bbbbbbbbbbbbbbbbbbbbbbbb', origin: 'subscription_recurring', billing_period: { starts_at: date(30 * day), ends_at: date(60 * day) } })
  renewal.occurred_at = date(30 * day)
  assert.equal((await webhook(env, renewal)).status, 200)
  const older = transaction(purchase.session_id)
  older.occurred_at = date(1000)
  await webhook(env, older)
  const key = env.sqlite.prepare('SELECT * FROM windows_keys').get()!
  assert.equal(key.key_id, keyBefore)
  assert.equal(key.expires_at, date(60 * day))
})

test('subscription snapshots before payment are retained; scheduled cancel keeps access', async () => {
  const env = environment()
  const purchase = await session(env)
  assert.equal((await webhook(env, subscription(purchase.session_id))).status, 200)
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 0)
  await webhook(env, transaction(purchase.session_id))
  assert.equal(env.sqlite.prepare('SELECT status FROM windows_keys').get()!.status, 'available')
})

test('effective cancellation wins over late paid webhook and stale active snapshot', async () => {
  const env = environment()
  const purchase = await session(env)
  await webhook(env, subscription(purchase.session_id, 'canceled', 3000))
  await webhook(env, transaction(purchase.session_id))
  assert.equal(env.sqlite.prepare('SELECT status FROM windows_keys').get()!.status, 'revoked')
  await webhook(env, subscription(purchase.session_id, 'active', 1000))
  assert.equal(env.sqlite.prepare('SELECT status FROM windows_keys').get()!.status, 'revoked')
})

test('pause revokes, resume restores previous device limits, unpaid period never extends expiry', async () => {
  const env = environment()
  const purchase = await session(env)
  await webhook(env, transaction(purchase.session_id))
  env.sqlite.prepare("UPDATE windows_keys SET activation_count=1, device_hash='existing-device', status='activated'").run()
  await webhook(env, subscription(purchase.session_id, 'paused', 1000))
  assert.equal(env.sqlite.prepare('SELECT status FROM windows_keys').get()!.status, 'revoked')
  const resumed = subscription(purchase.session_id, 'active', 2000)
  resumed.data.current_billing_period!.ends_at = date(60 * day)
  await webhook(env, resumed)
  const key = env.sqlite.prepare('SELECT * FROM windows_keys').get()!
  assert.equal(key.status, 'activated')
  assert.equal(key.activation_count, 1)
  assert.equal(key.expires_at, date(30 * day))
})

test('a session cannot provision two subscriptions', async () => {
  const env = environment()
  const purchase = await session(env)
  await webhook(env, transaction(purchase.session_id))
  const second = transaction(purchase.session_id, { subscription_id: 'sub_01bbbbbbbbbbbbbbbbbbbbbbbb' })
  assert.equal((await webhook(env, second)).status, 422)
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 1)
})

test('expired claim token and oversized webhook are rejected', async () => {
  const env = environment()
  const purchase = await session(env)
  env.sqlite.prepare('UPDATE paddle_checkout_sessions SET claim_expires_at=0').run()
  assert.equal((await claim(env, purchase)).status, 410)
  const raw = 'a'.repeat(262_145)
  const oversized = await handlePaddleRequest(new Request('https://api.xlsone.com/api/webhook/paddle', { method: 'POST', body: raw, headers: { 'Paddle-Signature': signature(raw) } }), env as never)
  assert.equal(oversized.status, 413)
})

test('POST claim uses the same bearer credential and never includes it in its response', async () => {
  const env = environment()
  const purchase = await session(env)
  await webhook(env, transaction(purchase.session_id))
  const response = await handlePaddleRequest(new Request('https://api.xlsone.com/api/billing/claim', {
    method: 'POST', headers: { Authorization: `Bearer ${purchase.claim_token}`, 'CF-Connecting-IP': '203.0.113.1' },
    body: JSON.stringify({ session_id: purchase.session_id }),
  }), env as never)
  assert.equal(response.status, 200)
  const text = await response.text()
  assert.equal(text.includes(purchase.claim_token), false)
  assert.equal(response.headers.get('Cache-Control'), 'no-store')
})

test('forged and unsigned webhooks cannot provision any license', async () => {
  const env = environment()
  const purchase = await session(env)
  const raw = JSON.stringify(transaction(purchase.session_id))
  for (const sig of ['', signature(raw + ' ')]) {
    const response = await handlePaddleRequest(new Request('https://api.xlsone.com/api/webhook/paddle', {
      method: 'POST', headers: { 'Paddle-Signature': sig }, body: raw,
    }), env as never)
    assert.equal(response.status, 401)
  }
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 0)
})

test('rate limiting bounds anonymous session creation', async () => {
  const env = environment()
  for (let i = 0; i < 30; i++) await session(env)
  const response = await handlePaddleRequest(new Request('https://api.xlsone.com/api/billing/session', {
    method: 'POST', headers: { 'CF-Connecting-IP': '203.0.113.1' }, body: '{"plan":"monthly"}',
  }), env as never)
  assert.equal(response.status, 429)
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM paddle_checkout_sessions').get()!.n, 30)
})

test('expired purchase sessions cannot provision a new subscription', async () => {
  const env = environment()
  const purchase = await session(env)
  env.sqlite.prepare('UPDATE paddle_checkout_sessions SET purchase_expires_at=0').run()
  assert.equal((await webhook(env, transaction(purchase.session_id))).status, 422)
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 0)
})

test('concurrent initial payment and cancellation converge atomically without granting access', async () => {
  const env = environment()
  const purchase = await session(env)
  const responses = await Promise.all([
    webhook(env, transaction(purchase.session_id)),
    webhook(env, subscription(purchase.session_id, 'canceled', 1000)),
  ])
  assert.deepEqual(responses.map(response => response.status), [200, 200])
  assert.equal(env.sqlite.prepare('SELECT count(*) AS n FROM windows_keys').get()!.n, 1)
  assert.equal(env.sqlite.prepare('SELECT status FROM windows_keys').get()!.status, 'revoked')
})
