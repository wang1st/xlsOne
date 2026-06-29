// xlsOne Activation API — Cloudflare Worker
// Endpoints:
//   POST /api/activate  — 激活许可证密钥
//   POST /api/verify    — 验证 License Token
//   POST /api/refresh   — 刷新订阅 Token
//   POST /api/webhook/lemonsqueezy — Lemon Squeezy 购买回调
//   POST /api/admin/generate — 管理后台生成激活码（需 API Key）

import { Hono } from 'hono'
import { cors } from 'hono/cors'
import { createHmac, timingSafeEqual } from './crypto'

// ========== Types ==========

interface Env {
  DB: D1Database
  ACTIVATION_SECRET: string
  LEMON_SQUEEZY_SIGNING_SECRET: string
  ADMIN_API_KEY?: string
}

interface LicensePayload {
  sub: string       // key_id
  dev: string       // device_id
  plan: string      // plan type
  iat: number       // issued at
  exp: number       // expiration timestamp (0 = lifetime)
}

// ========== Constants ==========

const KEY_PATTERN = /^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$/
const TOKEN_LIFETIME = 30 * 24 * 60 * 60  // 30 days in seconds
const MIGRATION_LIMIT = 3                 // max device migrations per key per year
const RATE_LIMIT_WINDOW = 60              // 1 minute
const RATE_LIMIT_MAX = 10                 // max requests per window

// ========== Helpers ==========

function b64enc(buf: ArrayBuffer): string {
  return btoa(String.fromCharCode(...new Uint8Array(buf)))
    .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

function b64dec(str: string): ArrayBuffer {
  str = str.replace(/-/g, '+').replace(/_/g, '/')
  while (str.length % 4) str += '='
  const binary = atob(str)
  const bytes = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i)
  return bytes.buffer
}

async function sign(payload: LicensePayload, secret: string): Promise<string> {
  const encoder = new TextEncoder()
  const header = b64enc(encoder.encode(JSON.stringify({ alg: 'HS256', typ: 'JWT' })))
  const body = b64enc(encoder.encode(JSON.stringify(payload)))

  const key = await crypto.subtle.importKey(
    'raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']
  )
  const sig = await crypto.subtle.sign('HMAC', key, encoder.encode(`${header}.${body}`))
  return `${header}.${body}.${b64enc(sig)}`
}

async function verifyToken(token: string, secret: string): Promise<LicensePayload | null> {
  try {
    const parts = token.split('.')
    if (parts.length !== 3) return null

    const encoder = new TextEncoder()
    const key = await crypto.subtle.importKey(
      'raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['verify']
    )
    const valid = await crypto.subtle.verify(
      'HMAC', key,
      new Uint8Array(b64dec(parts[2])),
      encoder.encode(`${parts[0]}.${parts[1]}`)
    )
    if (!valid) return null

    const payload: LicensePayload = JSON.parse(
      new TextDecoder().decode(b64dec(parts[1]))
    )
    return payload
  } catch {
    return null
  }
}

function generateKeyId(): string {
  const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789' // no 0/O/I/1 for readability
  const part = () => Array.from({ length: 4 }, () => chars[Math.floor(Math.random() * chars.length)]).join('')
  return `XLS1-${part()}-${part()}`
}

// Simple in-memory rate limiter (per-IP)
const rateMap = new Map<string, { count: number; reset: number }>()

function checkRateLimit(ip: string): boolean {
  const now = Math.floor(Date.now() / 1000)
  const entry = rateMap.get(ip)
  if (!entry || entry.reset < now) {
    rateMap.set(ip, { count: 1, reset: now + RATE_LIMIT_WINDOW })
    return true
  }
  if (entry.count >= RATE_LIMIT_MAX) return false
  entry.count++
  return true
}

// ========== App ==========

const app = new Hono<{ Bindings: Env }>()

// CORS
app.use('*', cors({
  origin: ['https://z-pulse.cn', 'https://xlsone.com', 'app://xlsone'],
  allowMethods: ['POST', 'OPTIONS'],
  allowHeaders: ['Content-Type', 'Authorization'],
  maxAge: 86400,
}))

// ========== POST /api/activate ==========

app.post('/api/activate', async (c) => {
  const ip = c.req.header('CF-Connecting-IP') || 'unknown'
  if (!checkRateLimit(ip)) {
    return c.json({ error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' }, 429)
  }

  let body: { key: string; device_id: string; device_name?: string }
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const { key, device_id, device_name } = body

  // Validate inputs
  if (!key || !KEY_PATTERN.test(key.toUpperCase())) {
    return c.json({ error: 'INVALID_KEY', message: '激活码格式不正确' }, 400)
  }
  if (!device_id || device_id.length < 8 || device_id.length > 128) {
    return c.json({ error: 'INVALID_DEVICE', message: '设备标识无效' }, 400)
  }

  const keyId = key.toUpperCase()

  // Look up key
  const keyRow = await c.env.DB.prepare(
    'SELECT * FROM activation_keys WHERE key_id = ?'
  ).bind(keyId).first<{
    key_id: string; product: string; plan: string; status: string
    max_devices: number; expires_at: string | null
  }>()

  if (!keyRow) {
    return c.json({ error: 'KEY_NOT_FOUND', message: '激活码不存在' }, 404)
  }

  if (keyRow.status === 'revoked') {
    return c.json({ error: 'KEY_REVOKED', message: '激活码已被吊销' }, 403)
  }

  // Count existing devices for this key
  const deviceCount = await c.env.DB.prepare(
    'SELECT COUNT(*) as count FROM devices WHERE key_id = ? AND revoked = 0'
  ).bind(keyId).first<{ count: number }>()

  if (deviceCount && deviceCount.count >= keyRow.max_devices) {
    // Check if this device already activated this key
    const existing = await c.env.DB.prepare(
      'SELECT device_id FROM devices WHERE key_id = ? AND device_id = ? AND revoked = 0'
    ).bind(keyId, device_id).first()

    if (!existing) {
      return c.json({
        error: 'DEVICE_LIMIT',
        message: `该激活码已达到最大设备数限制（${keyRow.max_devices}台）`,
        current_devices: deviceCount.count,
        max_devices: keyRow.max_devices,
      }, 403)
    }
  }

  // Check subscription expiry
  if (keyRow.expires_at) {
    const expiry = new Date(keyRow.expires_at)
    if (expiry < new Date()) {
      return c.json({ error: 'SUBSCRIPTION_EXPIRED', message: '订阅已过期' }, 403)
    }
  }

  // Issue license token
  const now = Math.floor(Date.now() / 1000)
  let exp = 0 // 0 = lifetime/permanent
  if (keyRow.expires_at) {
    exp = Math.floor(new Date(keyRow.expires_at).getTime() / 1000)
    // Cap token expiry at TOKEN_LIFETIME for refresh cycle
    exp = Math.min(exp, now + TOKEN_LIFETIME)
  }

  const payload: LicensePayload = {
    sub: keyId,
    dev: device_id,
    plan: keyRow.plan,
    iat: now,
    exp,
  }

  const token = await sign(payload, c.env.ACTIVATION_SECRET)

  // Upsert device record
  await c.env.DB.prepare(`
    INSERT INTO devices (device_id, key_id, license_token, device_name, activated_at, last_seen_at)
    VALUES (?, ?, ?, ?, datetime('now'), datetime('now'))
    ON CONFLICT(device_id) DO UPDATE SET
      license_token = excluded.license_token,
      last_seen_at = datetime('now'),
      revoked = 0
  `).bind(device_id, keyId, token, device_name || '').run()

  // Mark key as used if first activation
  if (keyRow.status === 'unused') {
    await c.env.DB.prepare(
      "UPDATE activation_keys SET status = 'used', used_at = datetime('now') WHERE key_id = ?"
    ).bind(keyId).run()
  }

  return c.json({
    license_token: token,
    plan: keyRow.plan,
    expires_at: keyRow.expires_at || null,
    device_id,
  })
})

// ========== POST /api/verify ==========

app.post('/api/verify', async (c) => {
  let body: { token: string; device_id: string }
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const { token, device_id } = body

  if (!token || !device_id) {
    return c.json({ error: 'MISSING_PARAMS' }, 400)
  }

  const payload = await verifyToken(token, c.env.ACTIVATION_SECRET)
  if (!payload) {
    return c.json({ error: 'INVALID_TOKEN', message: '许可证无效' }, 401)
  }

  // Verify device matches
  if (payload.dev !== device_id) {
    return c.json({ error: 'DEVICE_MISMATCH', message: '设备不匹配' }, 403)
  }

  // Check if subscription expired
  if (payload.exp > 0 && payload.exp < Math.floor(Date.now() / 1000)) {
    return c.json({ error: 'TOKEN_EXPIRED', message: '许可证已过期，请续费', expired: true }, 403)
  }

  // Check if device not revoked
  const device = await c.env.DB.prepare(
    'SELECT revoked FROM devices WHERE device_id = ? AND key_id = ?'
  ).bind(device_id, payload.sub).first<{ revoked: number }>()

  if (!device || device.revoked) {
    return c.json({ error: 'DEVICE_REVOKED', message: '此设备授权已被撤销' }, 403)
  }

  // Update last seen
  await c.env.DB.prepare(
    "UPDATE devices SET last_seen_at = datetime('now') WHERE device_id = ?"
  ).bind(device_id).run()

  return c.json({
    valid: true,
    plan: payload.plan,
    expires_at: payload.exp > 0 ? new Date(payload.exp * 1000).toISOString() : null,
  })
})

// ========== POST /api/refresh ==========

app.post('/api/refresh', async (c) => {
  let body: { token: string; device_id: string }
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const { token, device_id } = body
  const payload = await verifyToken(token, c.env.ACTIVATION_SECRET)
  if (!payload || payload.dev !== device_id) {
    return c.json({ error: 'INVALID_TOKEN' }, 401)
  }

  // For lifetime licenses, no need to refresh
  if (payload.exp === 0) {
    return c.json({ valid: true, token, refreshed: false })
  }

  // Check key status in DB
  const keyRow = await c.env.DB.prepare(
    'SELECT * FROM activation_keys WHERE key_id = ?'
  ).bind(payload.sub).first<{ status: string; expires_at: string | null }>()

  if (!keyRow || keyRow.status === 'revoked') {
    return c.json({ error: 'KEY_REVOKED' }, 403)
  }

  if (keyRow.expires_at) {
    const expiry = new Date(keyRow.expires_at)
    if (expiry < new Date()) {
      return c.json({ error: 'SUBSCRIPTION_EXPIRED' }, 403)
    }
  }

  // Issue new token
  const now = Math.floor(Date.now() / 1000)
  let exp = 0
  if (keyRow.expires_at) {
    exp = Math.floor(new Date(keyRow.expires_at).getTime() / 1000)
    exp = Math.min(exp, now + TOKEN_LIFETIME)
  }

  const newPayload: LicensePayload = {
    sub: payload.sub,
    dev: device_id,
    plan: payload.plan,
    iat: now,
    exp,
  }
  const newToken = await sign(newPayload, c.env.ACTIVATION_SECRET)

  // Update device record
  await c.env.DB.prepare(
    "UPDATE devices SET license_token = ?, last_seen_at = datetime('now') WHERE device_id = ?"
  ).bind(newToken, device_id).run()

  return c.json({ valid: true, token: newToken, refreshed: true })
})

// ========== POST /api/webhook/lemonsqueezy ==========

app.post('/api/webhook/lemonsqueezy', async (c) => {
  const signature = c.req.header('X-Signature')
  if (!signature || !c.env.LEMON_SQUEEZY_SIGNING_SECRET) {
    return c.json({ error: 'UNAUTHORIZED' }, 401)
  }

  const body = await c.req.text()
  const encoder = new TextEncoder()
  const key = await crypto.subtle.importKey(
    'raw', encoder.encode(c.env.LEMON_SQUEEZY_SIGNING_SECRET),
    { name: 'HMAC', hash: 'SHA-256' }, false, ['verify']
  )

  const sigBytes = new Uint8Array(signature.match(/.{1,2}/g)!.map(b => parseInt(b, 16)))
  const valid = await crypto.subtle.verify(
    'HMAC', key, sigBytes, encoder.encode(body)
  )

  if (!valid) {
    return c.json({ error: 'INVALID_SIGNATURE' }, 401)
  }

  const event = JSON.parse(body)
  const eventName = event.meta?.event_name
  const orderData = event.data?.attributes

  if (eventName === 'order_created' && orderData?.status === 'paid') {
    const email = orderData.user_email || ''
    const plan = orderData.variant_name?.toLowerCase().includes('lifetime')
      ? 'personal_lifetime'
      : orderData.variant_name?.toLowerCase().includes('enterprise')
        ? 'enterprise_10'
        : 'personal_yearly'

    const keyId = generateKeyId()
    const expiresAt = plan === 'personal_lifetime' ? null
      : new Date(Date.now() + 365 * 24 * 60 * 60 * 1000).toISOString()

    await c.env.DB.prepare(`
      INSERT INTO activation_keys (key_id, product, plan, status, max_devices, expires_at, order_id, email)
      VALUES (?, 'xlsone', ?, 'unused', ?, ?, ?, ?)
    `).bind(keyId, plan, plan.startsWith('enterprise') ? 10 : 1, expiresAt, orderData.id, email).run()

    // TODO: Send email with key via Resend/SendGrid
    return c.json({ key_id: keyId, email, status: 'created' })
  }

  return c.json({ status: 'ignored' })
})

// ========== POST /api/admin/generate (管理后台生成激活码) ==========

app.post('/api/admin/generate', async (c) => {
  const auth = c.req.header('Authorization')
  const apiKey = c.env.ADMIN_API_KEY || c.env.ACTIVATION_SECRET
  if (!auth || auth !== `Bearer ${apiKey}`) {
    return c.json({ error: 'UNAUTHORIZED' }, 401)
  }

  let body: { count?: number; plan?: string; max_devices?: number; expires_at?: string }
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const count = Math.min(body.count || 1, 100)
  const plan = body.plan || 'personal_lifetime'
  const maxDevices = body.max_devices || 1

  const keys: string[] = []
  const stmt = c.env.DB.prepare(
    'INSERT INTO activation_keys (key_id, product, plan, status, max_devices, expires_at) VALUES (?, ?, ?, ?, ?, ?)'
  )

  const batch = []
  for (let i = 0; i < count; i++) {
    const keyId = generateKeyId()
    batch.push(stmt.bind(keyId, 'xlsone', plan, 'unused', maxDevices, body.expires_at || null))
    keys.push(keyId)
  }

  await c.env.DB.batch(batch)

  return c.json({ keys, count, plan })
})

// ========== Health check ==========

app.get('/api/health', (c) => c.json({ status: 'ok', timestamp: new Date().toISOString() }))

export default app
