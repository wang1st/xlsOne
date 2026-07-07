// xlsOne Activation API — Cloudflare Worker
// Endpoints:
//   POST /api/activate          — 激活许可证密钥（旧版 HMAC/JWT，保留兼容）
//   POST /api/verify            — 验证 License Token
//   POST /api/refresh           — 刷新订阅 Token
//   POST /api/webhook/lemonsqueezy — Lemon Squeezy 购买回调
//   POST /api/admin/generate    — 管理后台生成激活码
//   POST /api/activate/windows  — Windows 版 Ed25519 许可证激活
//   POST /api/webhook/afdian    — 爱发电购买回调（Windows 版）
//   POST /api/admin/generate-windows — 管理后台生成 Windows 激活码

import { Hono } from 'hono'
import { cors } from 'hono/cors'
import { ed25519 } from '@noble/curves/ed25519'
import { createHmac, timingSafeEqual } from './crypto'
import { md5 } from 'js-md5'

// ========== Types ==========

interface Env {
  DB: D1Database
  ACTIVATION_SECRET: string
  LEMON_SQUEEZY_SIGNING_SECRET: string
  ED25519_PRIVATE_KEY?: string
  AFDIAN_USER_ID?: string
  AFDIAN_TOKEN?: string
  AFDIAN_WEBHOOK_TOKEN?: string
  ADMIN_API_KEY?: string
}

interface LicensePayload {
  sub: string       // key_id
  dev: string       // device_id
  plan: string      // plan type
  iat: number       // issued at
  exp: number       // expiration timestamp (0 = lifetime)
}

interface WindowsLicensePayload {
  key_id: string
  plan: string
  device_hash: string
  device_components: string[]
  issued_at: number
  expires_at: number
}

// ========== Constants ==========

const KEY_PATTERN = /^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$/
const WINDOWS_KEY_PATTERN = /^XLS1-[A-Z0-9]{4}-[A-Z0-9]{4}$/
const TOKEN_LIFETIME = 30 * 24 * 60 * 60  // 30 days in seconds
const MIGRATION_LIMIT = 3                 // max device migrations per key per year
const RATE_LIMIT_WINDOW = 60              // 1 minute
const RATE_LIMIT_MAX = 10                 // max requests per window

// ========== Helpers ==========

function b64enc(buf: ArrayBuffer | Uint8Array): string {
  return btoa(String.fromCharCode(...new Uint8Array(buf)))
    .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

function utf8ToBase64(str: string): string {
  const bytes = new TextEncoder().encode(str)
  return btoa(String.fromCharCode(...bytes))
}

function b64dec(str: string): ArrayBuffer {
  str = str.replace(/-/g, '+').replace(/_/g, '/')
  while (str.length % 4) str += '='
  const binary = atob(str)
  const bytes = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i)
  return bytes.buffer
}

function hexToBytes(hex: string): Uint8Array {
  const bytes = new Uint8Array(hex.length / 2)
  for (let i = 0; i < bytes.length; i++) {
    bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16)
  }
  return bytes
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('')
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

// ========== Ed25519 license helpers ==========

function getEd25519PrivateKey(env: Env): Uint8Array | null {
  if (!env.ED25519_PRIVATE_KEY) return null
  const hex = env.ED25519_PRIVATE_KEY.replace(/[^0-9a-fA-F]/g, '')
  if (hex.length !== 64) return null
  return hexToBytes(hex)
}

function signWindowsLicense(payload: WindowsLicensePayload, privateKey: Uint8Array): string {
  const encoder = new TextEncoder()
  const message = encoder.encode(JSON.stringify(payload))
  const signature = ed25519.sign(message, privateKey)
  return b64enc(signature)
}

function buildWindowsLicense(
  keyRow: { key_id: string; plan: string; expires_at: string | null; device_hash: string | null; device_components: string | null },
  deviceHash: string,
  deviceComponents: string[],
  privateKey: Uint8Array
): { license: object; payload: WindowsLicensePayload } {
  const now = Math.floor(Date.now() / 1000)
  let expiresAt = 0
  if (keyRow.expires_at) {
    expiresAt = Math.floor(new Date(keyRow.expires_at).getTime() / 1000)
  }

  const payload: WindowsLicensePayload = {
    key_id: keyRow.key_id,
    plan: keyRow.plan,
    device_hash: deviceHash,
    device_components: deviceComponents,
    issued_at: now,
    expires_at: expiresAt,
  }

  const signature = signWindowsLicense(payload, privateKey)

  const license = {
    ...payload,
    signature,
  }

  return { license, payload }
}

async function countMigrations(db: D1Database, keyId: string, since: Date): Promise<number> {
  const result = await db.prepare(
    'SELECT COUNT(*) as count FROM windows_device_migrations WHERE key_id = ? AND migrated_at >= ?'
  ).bind(keyId, since.toISOString()).first<{ count: number }>()
  return result?.count || 0
}

// ========== Afdian helpers ==========

function afdianSign(token: string, paramsBase64: string, timestamp: number): string {
  // 爱发电开放平台签名：MD5(token + params_base64 + timestamp + token)
  return md5(`${token}${paramsBase64}${timestamp}${token}`)
}

async function afdianSendMessage(
  env: Env,
  receiverId: string,
  message: string
): Promise<{ success: boolean; error?: string }> {
  if (!env.AFDIAN_USER_ID || !env.AFDIAN_TOKEN) {
    return { success: false, error: 'Afdian credentials not configured' }
  }

  const params = {
    receiver_id: receiverId,
    message,
  }
  const paramsStr = JSON.stringify(params)
  const paramsBase64 = utf8ToBase64(paramsStr)
  const timestamp = Math.floor(Date.now() / 1000)
  const sign = afdianSign(env.AFDIAN_TOKEN, paramsBase64, timestamp)

  const body = {
    user_id: env.AFDIAN_USER_ID,
    params: paramsBase64,
    ts: timestamp,
    sign,
  }

  try {
    const res = await fetch('https://afdian.net/api/open/send-msg', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    const data = await res.json<{ ec?: number; msg?: string }>()
    if (data.ec === 200) {
      return { success: true }
    }
    return { success: false, error: data.msg || `ec=${data.ec}` }
  } catch (err) {
    return { success: false, error: String(err) }
  }
}

function verifyAfdianWebhook(token: string, data: string, sign: string): boolean {
  // Afdian signs over the EXACT raw `data` string: MD5(token + data + token).
  // `data` MUST be the un-parsed original string — re-serializing a parsed object
  // changes key order / whitespace and the signature will never match.
  const expected = md5(`${token}${data}${token}`)
  return timingSafeEqual(expected, sign.toLowerCase())
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

// ========== Windows license issuance helper ==========

interface WindowsIssueSuccess {
  ok: true
  license: object
  plan: string
  expires_at: string | null
  device_hash: string
}

interface WindowsIssueFailure {
  ok: false
  status: number
  body: object
}

type WindowsIssueResult = WindowsIssueSuccess | WindowsIssueFailure

async function issueWindowsLicense(
  env: Env,
  keyId: string,
  deviceHash: string,
  deviceName: string,
  deviceComponents: string[]
): Promise<WindowsIssueResult> {
  const keyRow = await env.DB.prepare(
    'SELECT * FROM windows_keys WHERE key_id = ?'
  ).bind(keyId).first<{
    key_id: string
    plan: string
    status: string
    device_hash: string | null
    device_components: string | null
    expires_at: string | null
  }>()

  if (!keyRow) {
    return { ok: false, status: 404, body: { error: 'KEY_NOT_FOUND', message: '激活码不存在' } }
  }

  if (keyRow.status === 'revoked') {
    return { ok: false, status: 403, body: { error: 'KEY_REVOKED', message: '激活码已被吊销' } }
  }

  if (keyRow.expires_at) {
    const expiry = new Date(keyRow.expires_at)
    if (expiry < new Date()) {
      return { ok: false, status: 403, body: { error: 'SUBSCRIPTION_EXPIRED', message: '订阅已过期' } }
    }
  }

  if (keyRow.device_hash && keyRow.device_hash !== deviceHash) {
    // Allow migration within limit.
    const oneYearAgo = new Date(Date.now() - 365 * 24 * 60 * 60 * 1000)
    const migrations = await countMigrations(env.DB, keyId, oneYearAgo)
    if (migrations >= MIGRATION_LIMIT) {
      return {
        ok: false,
        status: 403,
        body: {
          error: 'DEVICE_LIMIT',
          message: `该激活码每年最多换机 ${MIGRATION_LIMIT} 次，已达上限`,
        },
      }
    }

    // Record migration and revoke the previous device.
    await env.DB.prepare(`
      INSERT INTO windows_device_migrations (key_id, old_device, new_device)
      VALUES (?, ?, ?)
    `).bind(keyId, keyRow.device_hash, deviceHash).run()

    await env.DB.prepare(`
      UPDATE windows_devices SET revoked = 1 WHERE key_id = ? AND device_id = ? AND revoked = 0
    `).bind(keyId, keyRow.device_hash).run()
  }

  const privateKey = getEd25519PrivateKey(env)
  if (!privateKey) {
    return { ok: false, status: 500, body: { error: 'SERVER_ERROR', message: '签名密钥未配置' } }
  }

  const components = Array.isArray(deviceComponents) ? deviceComponents : []
  const { license } = buildWindowsLicense(keyRow, deviceHash, components, privateKey)

  // Upsert device record.
  await env.DB.prepare(`
    INSERT INTO windows_devices (device_id, key_id, device_name, activated_at, last_seen_at)
    VALUES (?, ?, ?, datetime('now'), datetime('now'))
    ON CONFLICT(device_id, key_id) DO UPDATE SET
      last_seen_at = datetime('now'),
      revoked = 0
  `).bind(deviceHash, keyId, deviceName || '').run()

  // Update key binding.
  await env.DB.prepare(`
    UPDATE windows_keys
    SET status = 'used',
        device_hash = ?,
        device_components = ?,
        activated_at = datetime('now')
    WHERE key_id = ?
  `).bind(deviceHash, JSON.stringify(components), keyId).run()

  return {
    ok: true,
    license,
    plan: keyRow.plan,
    expires_at: keyRow.expires_at || null,
    device_hash: deviceHash,
  }
}

function parseWindowsActivationBody(body: unknown): {
  key: string
  device_hash: string
  device_name?: string
  device_components?: string[]
} | null {
  if (typeof body !== 'object' || body === null) return null
  const b = body as Record<string, unknown>
  if (typeof b.key !== 'string' || typeof b.device_hash !== 'string') return null
  return {
    key: b.key,
    device_hash: b.device_hash,
    device_name: typeof b.device_name === 'string' ? b.device_name : undefined,
    device_components: Array.isArray(b.device_components)
      ? b.device_components.filter((x): x is string => typeof x === 'string')
      : undefined,
  }
}

// ========== POST /api/activate/windows ==========

app.post('/api/activate/windows', async (c) => {
  const ip = c.req.header('CF-Connecting-IP') || 'unknown'
  if (!checkRateLimit(ip)) {
    return c.json({ error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' }, 429)
  }

  let body: unknown
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const parsed = parseWindowsActivationBody(body)
  if (!parsed) {
    return c.json({ error: 'INVALID_PARAMS', message: '请求参数无效' }, 400)
  }

  const { key, device_hash, device_name, device_components } = parsed

  if (!WINDOWS_KEY_PATTERN.test(key.toUpperCase())) {
    return c.json({ error: 'INVALID_KEY', message: '激活码格式不正确' }, 400)
  }
  if (device_hash.length < 16 || device_hash.length > 128) {
    return c.json({ error: 'INVALID_DEVICE', message: '设备指纹无效' }, 400)
  }

  const keyId = key.toUpperCase()
  const result = await issueWindowsLicense(
    c.env,
    keyId,
    device_hash,
    device_name || '',
    device_components || []
  )

  if (!result.ok) {
    return c.json(result.body, result.status as any)
  }

  return c.json({
    license: result.license,
    plan: result.plan,
    expires_at: result.expires_at,
    device_hash: result.device_hash,
  })
})

// ========== POST /api/license/download ==========
// For offline activation: user provides key + device fingerprint on a web page,
// server returns a signed .license file that can be imported into the Windows app.

app.post('/api/license/download', async (c) => {
  const ip = c.req.header('CF-Connecting-IP') || 'unknown'
  if (!checkRateLimit(ip)) {
    return c.json({ error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' }, 429)
  }

  let body: unknown
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const parsed = parseWindowsActivationBody(body)
  if (!parsed) {
    return c.json({ error: 'INVALID_PARAMS', message: '请求参数无效' }, 400)
  }

  const { key, device_hash, device_name, device_components } = parsed

  if (!WINDOWS_KEY_PATTERN.test(key.toUpperCase())) {
    return c.json({ error: 'INVALID_KEY', message: '激活码格式不正确' }, 400)
  }
  if (device_hash.length < 16 || device_hash.length > 128) {
    return c.json({ error: 'INVALID_DEVICE', message: '设备指纹无效' }, 400)
  }

  const keyId = key.toUpperCase()
  const result = await issueWindowsLicense(
    c.env,
    keyId,
    device_hash,
    device_name || '',
    device_components || []
  )

  if (!result.ok) {
    return c.json(result.body, result.status as any)
  }

  const licenseText = JSON.stringify(result.license)
  return c.body(licenseText, 200, {
    'Content-Type': 'application/json',
    'Content-Disposition': `attachment; filename="${keyId}.license"`,
  })
})

// ========== POST /api/webhook/afdian ==========

app.post('/api/webhook/afdian', async (c) => {
  if (!c.env.AFDIAN_WEBHOOK_TOKEN) {
    return c.json({ error: 'UNAUTHORIZED', message: 'Webhook token not configured' }, 401)
  }

  // Read the RAW body so we can verify the signature over the exact `data` string.
  const raw = await c.req.text()
  let payload: { data?: unknown; sign?: string }
  try {
    payload = JSON.parse(raw)
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const { data, sign } = payload
  // `data` must be the original string Afdian signed; do NOT parse it first.
  if (typeof sign !== 'string' || typeof data !== 'string' || !data) {
    return c.json({ error: 'INVALID_PAYLOAD' }, 400)
  }

  if (!verifyAfdianWebhook(c.env.AFDIAN_WEBHOOK_TOKEN, data, sign)) {
    return c.json({ error: 'INVALID_SIGNATURE' }, 401)
  }

  // Signature is valid — now parse the order from the verified string.
  let order: any
  try {
    order = JSON.parse(data)
  } catch {
    return c.json({ error: 'INVALID_ORDER' }, 400)
  }

  const orderObj = order?.order ?? order
  if (!orderObj || !orderObj.out_trade_no) {
    return c.json({ error: 'INVALID_ORDER' }, 400)
  }

  // Only issue a license once payment is actually settled. Afdian may send
  // webhooks for pending/refunding states too — never issue on those.
  const paid = ['PAID', 'paid', 'SUCCESS', 'success'].includes(
    orderObj.pay_status ?? orderObj.status ?? orderObj.order_status ?? ''
  )
  if (!paid) {
    return c.json({ status: 'ignored', reason: 'not_paid' })
  }

  const orderId = orderObj.out_trade_no
  const userId = orderObj.user_id
  const email = orderObj.remark || orderObj.user_name || ''
  // Windows 版当前为买断制；若以后按 SKU 区分档位，在此根据 orderObj.sku/title 映射。
  const plan = 'personal_lifetime'

  // Idempotency: 同一订单只发一次授权码。
  const existing = await c.env.DB.prepare(
    'SELECT key_id FROM windows_keys WHERE order_id = ?'
  ).bind(orderId).first<{ key_id: string }>()

  if (existing) {
    return c.json({ key_id: existing.key_id, order_id: orderId, status: 'exists' })
  }

  const keyId = generateKeyId()

  await c.env.DB.prepare(`
    INSERT INTO windows_keys (key_id, plan, status, order_id, email)
    VALUES (?, ?, 'unused', ?, ?)
  `).bind(keyId, plan, orderId, email).run()

  // Best-effort send private message with the key.
  let messageStatus = 'pending'
  if (userId && c.env.AFDIAN_USER_ID && c.env.AFDIAN_TOKEN) {
    const message = `感谢您的购买！\n表表归一 Windows 版激活码：${keyId}\n请在软件激活窗口输入此激活码完成绑定。激活码永久有效，每年可换机 ${MIGRATION_LIMIT} 次。`
    const sendResult = await afdianSendMessage(c.env, userId, message)
    messageStatus = sendResult.success ? 'sent' : `failed: ${sendResult.error}`
  }

  return c.json({ key_id: keyId, order_id: orderId, status: 'created', message_status: messageStatus })
})

// ========== POST /api/admin/generate-windows-keys ==========

app.post('/api/admin/generate-windows-keys', async (c) => {
  const auth = c.req.header('Authorization')
  const apiKey = c.env.ADMIN_API_KEY || c.env.ACTIVATION_SECRET
  if (!auth || auth !== `Bearer ${apiKey}`) {
    return c.json({ error: 'UNAUTHORIZED' }, 401)
  }

  let body: { count?: number; plan?: string; expires_at?: string }
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const count = Math.min(body.count || 1, 100)
  const plan = body.plan || 'personal_lifetime'

  const keys: string[] = []
  const stmt = c.env.DB.prepare(
    'INSERT INTO windows_keys (key_id, plan, status, expires_at) VALUES (?, ?, ?, ?)'
  )

  const batch = []
  for (let i = 0; i < count; i++) {
    const keyId = generateKeyId()
    batch.push(stmt.bind(keyId, plan, 'unused', body.expires_at || null))
    keys.push(keyId)
  }

  await c.env.DB.batch(batch)

  return c.json({ keys, count, plan })
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
