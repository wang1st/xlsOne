import type { Hono } from 'hono'

export interface PaddleEnv {
  DB: D1Database
  PADDLE_WEBHOOK_SECRET?: string
  PADDLE_MONTHLY_PRICE_ID?: string
  PADDLE_YEARLY_PRICE_ID?: string
}

const encoder = new TextEncoder()
const SESSION_PATTERN = /^pds_[a-f0-9]{32}$/
const TOKEN_PATTERN = /^[a-f0-9]{64}$/
const PRICE_PATTERN = /^pri_[a-z0-9]{26}$/
const MAX_WEBHOOK_BYTES = 262_144
const DAY = 86_400_000
const SNAPSHOT_EVENTS = new Set([
  'subscription.created', 'subscription.updated', 'subscription.activated',
  'subscription.canceled', 'subscription.paused', 'subscription.resumed',
  'subscription.past_due', 'subscription.trialing',
])
const SUBSCRIPTION_STATUSES = new Set(['active', 'past_due', 'trialing', 'paused', 'canceled'])

type JsonObject = Record<string, unknown>

interface CheckoutSession {
  session_id: string
  price_id: string
  created_at: number
  purchase_expires_at: number
  claim_expires_at: number
}

interface SubscriptionRow {
  subscription_id: string
  session_id: string
  key_id: string
  customer_id: string
}

class BodyLimitError extends Error {}

function object(value: unknown): JsonObject | null {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    ? value as JsonObject : null
}

function json(value: unknown, status = 200): Response {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' },
  })
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('')
}

function randomHex(length: number): string {
  return hex(crypto.getRandomValues(new Uint8Array(length)))
}

function activationKey(): string {
  const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
  const bytes = crypto.getRandomValues(new Uint8Array(16))
  return Array.from(bytes, byte => alphabet[byte & 31]).join('').match(/.{4}/g)!.join('-')
}

async function digest(value: string): Promise<string> {
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(value))))
}

async function limitedBody(request: Request, maximum: number): Promise<string> {
  const length = Number(request.headers.get('Content-Length'))
  if (length > maximum) throw new BodyLimitError()
  if (!request.body) return ''
  const reader = request.body.getReader()
  const chunks: Uint8Array[] = []
  let size = 0
  try {
    for (;;) {
      const { done, value } = await reader.read()
      if (done) break
      size += value.byteLength
      if (size > maximum) {
        await reader.cancel()
        throw new BodyLimitError()
      }
      chunks.push(value)
    }
  } finally { reader.releaseLock() }
  const bytes = new Uint8Array(size)
  let offset = 0
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength }
  return new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes)
}

/** Verify the unmodified body, including timestamp replay protection. */
export async function verifyPaddleSignature(
  rawBody: string,
  signature: string | null,
  secret: string,
  now = Date.now(),
): Promise<boolean> {
  if (!signature || signature.length > 1024 || !secret) return false
  const entries = signature.split(';').map(part => part.trim().split('='))
  const timestamps = entries.filter(([name]) => name === 'ts')
  const signatures = entries.filter(([name]) => name === 'h1')
  if (timestamps.length !== 1 || signatures.length < 1 || signatures.length > 5) return false
  const timestamp = timestamps[0][1]
  if (!/^\d{10,11}$/.test(timestamp ?? '')) return false
  if (Math.abs(Math.floor(now / 1000) - Number(timestamp)) > 300) return false
  const key = await crypto.subtle.importKey('raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['verify'])
  const payload = encoder.encode(`${timestamp}:${rawBody}`)
  for (const [, candidate] of signatures) {
    if (!/^[a-fA-F0-9]{64}$/.test(candidate ?? '')) continue
    const bytes = new Uint8Array(candidate.match(/../g)!.map(pair => parseInt(pair, 16)))
    if (await crypto.subtle.verify('HMAC', key, bytes, payload)) return true
  }
  return false
}

function allowedPrices(env: PaddleEnv): Set<string> {
  return new Set([env.PADDLE_MONTHLY_PRICE_ID, env.PADDLE_YEARLY_PRICE_ID].filter(
    (value): value is string => typeof value === 'string' && PRICE_PATTERN.test(value),
  ))
}

async function withinRateLimit(request: Request, env: PaddleEnv, action: string, maximum: number): Promise<boolean> {
  const now = Date.now()
  const hour = Math.floor(now / 3_600_000)
  const ipHash = await digest(request.headers.get('CF-Connecting-IP') || 'unknown')
  const bucket = `${action}:${hour}:${ipHash}`
  const row = await env.DB.prepare(`
    INSERT INTO paddle_rate_limits (bucket, requests, expires_at) VALUES (?, 1, ?)
    ON CONFLICT(bucket) DO UPDATE SET requests=requests+1 WHERE requests < ?
    RETURNING requests
  `).bind(bucket, (hour + 2) * 3_600_000, maximum).first()
  return row !== null
}

async function createSession(request: Request, env: PaddleEnv): Promise<Response> {
  if (!env.PADDLE_WEBHOOK_SECRET || !allowedPrices(env).size) return json({ error: 'BILLING_UNAVAILABLE' }, 503)
  const body = object(JSON.parse(await limitedBody(request, 1024)))
  const price = body?.plan === 'monthly' ? env.PADDLE_MONTHLY_PRICE_ID
    : body?.plan === 'yearly' ? env.PADDLE_YEARLY_PRICE_ID : undefined
  if (!price || !PRICE_PATTERN.test(price)) return json({ error: 'INVALID_PLAN' }, 400)
  if (!await withinRateLimit(request, env, 'session', 30)) return json({ error: 'RATE_LIMITED' }, 429)
  const now = Date.now()
  const sessionId = `pds_${randomHex(16)}`
  const claimToken = randomHex(32)
  await env.DB.batch([
    env.DB.prepare('DELETE FROM paddle_checkout_sessions WHERE claim_expires_at < ?').bind(now),
    env.DB.prepare('DELETE FROM paddle_rate_limits WHERE expires_at < ?').bind(now),
    env.DB.prepare(`INSERT INTO paddle_checkout_sessions
      (session_id, claim_hash, price_id, created_at, purchase_expires_at, claim_expires_at)
      VALUES (?, ?, ?, ?, ?, ?)`)
      .bind(sessionId, await digest(claimToken), price, now, now + DAY, now + 30 * DAY),
  ])
  return json({ session_id: sessionId, claim_token: claimToken, price_id: price, expires_at: new Date(now + 30 * DAY).toISOString() }, 201)
}

async function claimLicense(request: Request, env: PaddleEnv): Promise<Response> {
  const authorization = request.headers.get('Authorization') ?? ''
  const token = authorization.startsWith('Bearer ') ? authorization.slice(7) : ''
  if (!TOKEN_PATTERN.test(token)) return json({ error: 'UNAUTHORIZED' }, 401)
  const body = request.method === 'POST' ? object(JSON.parse(await limitedBody(request, 1024))) : null
  const sessionId = body?.session_id ?? new URL(request.url).searchParams.get('session_id')
  if (typeof sessionId !== 'string' || !SESSION_PATTERN.test(sessionId)) return json({ error: 'INVALID_SESSION' }, 400)
  if (!await withinRateLimit(request, env, 'claim', 300)) return json({ error: 'RATE_LIMITED' }, 429)
  const session = await env.DB.prepare(`SELECT * FROM paddle_checkout_sessions WHERE session_id=? AND claim_hash=?`)
    .bind(sessionId, await digest(token)).first<CheckoutSession>()
  if (!session) return json({ error: 'NOT_FOUND' }, 404)
  if (session.claim_expires_at < Date.now()) return json({ error: 'SESSION_EXPIRED', message: 'Contact support with your Paddle receipt to recover your activation key.' }, 410)
  const license = await env.DB.prepare(`SELECT k.key_id, k.expires_at, p.status AS subscription_status
    FROM paddle_subscriptions p JOIN windows_keys k ON k.key_id=p.key_id WHERE p.session_id=?`)
    .bind(sessionId).first<{ key_id: string; expires_at: string; subscription_status: string }>()
  if (!license) return json({ status: 'pending' }, 202)
  return json({ status: 'fulfilled', activation_key: license.key_id, expires_at: license.expires_at, subscription_status: license.subscription_status })
}

function timestamp(value: unknown): number | null {
  if (typeof value !== 'string' || !/^\d{4}-\d\d-\d\dT.*(?:Z|[+-]\d\d:\d\d)$/.test(value)) return null
  const result = Date.parse(value)
  return Number.isFinite(result) ? result : null
}

function periodEnd(value: unknown): string | null {
  const period = object(value)
  const start = timestamp(period?.starts_at)
  const end = timestamp(period?.ends_at)
  return start !== null && end !== null && end > start ? new Date(end).toISOString() : null
}

function paddleId(value: unknown, prefix: string): value is string {
  return typeof value === 'string' && new RegExp(`^${prefix}_[a-z0-9]{26}$`).test(value)
}

function eventPrice(data: JsonObject, env: PaddleEnv): string | null {
  if (!Array.isArray(data.items) || data.items.length !== 1) return null
  const item = object(data.items[0])
  const price = object(item?.price)?.id
  return item?.quantity === 1 && typeof price === 'string' && allowedPrices(env).has(price) ? price : null
}

function synchronizeLicense(env: PaddleEnv, subscriptionId: string): D1PreparedStatement {
  // Both lifecycle snapshots and completed payments converge through the latest
  // subscription row inside the SAME D1 transaction. An unpaid snapshot cannot
  // extend paid_until, and a late payment cannot undo effective cancellation.
  return env.DB.prepare(`
    INSERT INTO windows_keys (key_id, plan, status, max_activations, activation_count, duration_days, order_id, expires_at)
    SELECT key_id, 'personal_yearly',
      CASE WHEN status IN ('paused', 'canceled', 'trialing') THEN 'revoked' ELSE 'available' END,
      3, 0, 1, 'paddle:' || subscription_id, paid_until
    FROM paddle_subscriptions WHERE subscription_id=? AND paid_until IS NOT NULL
    ON CONFLICT(key_id) DO UPDATE SET expires_at=excluded.expires_at,
      status=CASE WHEN excluded.status='revoked' THEN 'revoked'
        WHEN windows_keys.activation_count >= windows_keys.max_activations THEN 'exhausted'
        WHEN windows_keys.device_hash IS NOT NULL THEN 'activated' ELSE 'available' END
  `).bind(subscriptionId)
}

async function receiveWebhook(request: Request, env: PaddleEnv): Promise<Response> {
  if (!env.PADDLE_WEBHOOK_SECRET || !allowedPrices(env).size) return json({ error: 'BILLING_UNAVAILABLE' }, 503)
  const rawBody = await limitedBody(request, MAX_WEBHOOK_BYTES)
  if (!await verifyPaddleSignature(rawBody, request.headers.get('Paddle-Signature'), env.PADDLE_WEBHOOK_SECRET)) {
    return json({ error: 'INVALID_SIGNATURE' }, 401)
  }
  const event = object(JSON.parse(rawBody))
  const data = object(event?.data)
  const eventId = event?.event_id
  const eventType = event?.event_type
  const occurredAt = timestamp(event?.occurred_at)
  if (!data || !paddleId(eventId, 'evt') || typeof eventType !== 'string' || occurredAt === null) {
    return json({ error: 'INVALID_EVENT' }, 400)
  }
  const payment = eventType === 'transaction.completed'
  if (!payment && !SNAPSHOT_EVENTS.has(eventType)) return json({ status: 'ignored' })
  const price = eventPrice(data, env)
  if (!price) return json({ status: 'ignored' })
  if (payment && !['web', 'api', 'subscription_recurring', 'subscription_update'].includes(String(data.origin))) {
    return json({ status: 'ignored' })
  }
  if (await env.DB.prepare('SELECT event_id FROM paddle_events WHERE event_id=?').bind(eventId).first()) {
    return json({ status: 'processed' })
  }
  const subscriptionId = payment ? data.subscription_id : data.id
  if (!paddleId(subscriptionId, 'sub') || !paddleId(data.customer_id, 'ctm')) return json({ error: 'INVALID_SUBSCRIPTION' }, 422)
  const customerId = data.customer_id
  const status = String(data.status)
  const end = periodEnd(payment ? data.billing_period : data.current_billing_period)
  if (payment && (status !== 'completed' || !paddleId(data.id, 'txn') || !end)) return json({ error: 'MISSING_PAID_PERIOD' }, 422)
  if (!payment && (!SUBSCRIPTION_STATUSES.has(status) || (!end && !['paused', 'canceled'].includes(status)))) {
    return json({ error: 'INVALID_SUBSCRIPTION_STATE' }, 422)
  }
  const existing = await env.DB.prepare('SELECT subscription_id, session_id, key_id, customer_id FROM paddle_subscriptions WHERE subscription_id=?')
    .bind(subscriptionId).first<SubscriptionRow>()
  let sessionId = existing?.session_id
  if (existing && existing.customer_id !== customerId) return json({ error: 'CUSTOMER_MISMATCH' }, 422)
  if (!existing) {
    const customSession = object(data.custom_data)?.xlsone_session_id
    if (typeof customSession !== 'string' || !SESSION_PATTERN.test(customSession)) return json({ error: 'MISSING_CHECKOUT_SESSION' }, 422)
    const session = await env.DB.prepare('SELECT * FROM paddle_checkout_sessions WHERE session_id=?').bind(customSession).first<CheckoutSession>()
    if (!session || session.price_id !== price || occurredAt > session.purchase_expires_at || occurredAt < session.created_at - 300_000) {
      return json({ error: 'INVALID_CHECKOUT_SESSION' }, 422)
    }
    const used = await env.DB.prepare('SELECT subscription_id FROM paddle_subscriptions WHERE session_id=?').bind(customSession).first()
    if (used) return json({ error: 'SESSION_ALREADY_USED' }, 422)
    sessionId = customSession
  }
  const keyId = existing?.key_id ?? activationKey()
  const statements: D1PreparedStatement[] = []
  if (payment) {
    statements.push(env.DB.prepare(`
      INSERT INTO paddle_subscriptions (subscription_id, session_id, key_id, customer_id, price_id, status,
        paid_until, last_payment_at, last_payment_event, last_transaction_id)
      VALUES (?, ?, ?, ?, ?, 'active', ?, ?, ?, ?)
      ON CONFLICT(subscription_id) DO UPDATE SET price_id=excluded.price_id,
        paid_until=MAX(COALESCE(paddle_subscriptions.paid_until, ''), excluded.paid_until),
        last_payment_at=excluded.last_payment_at, last_payment_event=excluded.last_payment_event,
        last_transaction_id=excluded.last_transaction_id
      WHERE excluded.last_payment_at > paddle_subscriptions.last_payment_at
        OR (excluded.last_payment_at=paddle_subscriptions.last_payment_at AND excluded.last_payment_event > paddle_subscriptions.last_payment_event)
    `).bind(subscriptionId, sessionId!, keyId, customerId, price, end!, occurredAt, eventId, data.id as string))
  } else {
    statements.push(env.DB.prepare(`
      INSERT INTO paddle_subscriptions (subscription_id, session_id, key_id, customer_id, price_id, status,
        current_period_end, last_snapshot_at, last_snapshot_event)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT(subscription_id) DO UPDATE SET price_id=excluded.price_id, status=excluded.status,
        current_period_end=excluded.current_period_end, last_snapshot_at=excluded.last_snapshot_at,
        last_snapshot_event=excluded.last_snapshot_event
      WHERE excluded.last_snapshot_at > paddle_subscriptions.last_snapshot_at
        OR (excluded.last_snapshot_at=paddle_subscriptions.last_snapshot_at AND excluded.last_snapshot_event > paddle_subscriptions.last_snapshot_event)
    `).bind(subscriptionId, sessionId!, keyId, customerId, price, status, end, occurredAt, eventId))
  }
  statements.push(synchronizeLicense(env, subscriptionId))
  statements.push(env.DB.prepare(`INSERT OR IGNORE INTO paddle_events (event_id, event_type, occurred_at, received_at) VALUES (?, ?, ?, ?)`)
    .bind(eventId, eventType, occurredAt, Date.now()))
  await env.DB.batch(statements)
  return json({ status: 'processed' })
}

/** Standalone handler is also used by integration tests with a real SQLite DB. */
export async function handlePaddleRequest(request: Request, env: PaddleEnv): Promise<Response> {
  const path = new URL(request.url).pathname
  try {
    if (path === '/api/billing/session' && request.method === 'POST') return await createSession(request, env)
    if (path === '/api/billing/claim' && ['POST', 'GET'].includes(request.method)) return await claimLicense(request, env)
    if (path === '/api/webhook/paddle' && request.method === 'POST') return await receiveWebhook(request, env)
    return json({ error: 'METHOD_NOT_ALLOWED' }, 405)
  } catch (error) {
    if (error instanceof BodyLimitError) return json({ error: 'BODY_TOO_LARGE' }, 413)
    if (error instanceof SyntaxError || error instanceof TypeError) return json({ error: 'INVALID_REQUEST' }, 400)
    // Do not leak database contents, claim tokens or webhook payloads to logs.
    console.error('[paddle] Request failed:', error instanceof Error ? error.name : 'UnknownError')
    return json({ error: 'BILLING_TEMPORARILY_UNAVAILABLE' }, 503)
  }
}

export function registerPaddleRoutes<E extends PaddleEnv>(app: Hono<{ Bindings: E }>): void {
  for (const path of ['/api/billing/session', '/api/billing/claim', '/api/webhook/paddle']) {
    app.all(path, context => handlePaddleRequest(context.req.raw, context.env))
  }
}
