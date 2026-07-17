// xlsOne Activation API — Cloudflare Worker
// Endpoints:
//   POST /api/activate          — 激活许可证密钥（旧版 HMAC/JWT，保留兼容）
//   POST /api/verify            — 验证 License Token
//   POST /api/refresh           — 刷新订阅 Token
//   POST /api/webhook/lemonsqueezy — Lemon Squeezy 购买回调
//   POST /api/admin/generate    — 管理后台生成激活码
//   POST /api/trial/windows  — Windows 版签名试用许可证
//   POST /api/activate/windows  — Windows 版 Ed25519 许可证激活
//   POST /api/admin/generate-windows-keys — 管理后台批量生成 Windows 激活码
//   GET  /api/admin/pool-stats  — 激活码池统计
//   POST /api/license/download  — 离线激活：下载签名 .license 文件
//   Note: 授权码生成器页面 (gift-code-generator.html) 可在本地浏览器打开，
//         或通过国内服务器 /xlsone/license-console 路由访问（路径可由 ADMIN_PATH 配置）。Worker 不提供静态页面托管。

import { Hono, Context } from 'hono'
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
const WINDOWS_KEY_PATTERN = /^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$/
const TOKEN_LIFETIME = 30 * 24 * 60 * 60  // 30 days in seconds
const RATE_LIMIT_WINDOW = 60              // 1 minute
const RATE_LIMIT_MAX = 10                 // max requests per window
const ADMIN_RATE_LIMIT_MAX = 30           // admin endpoints: looser (console pages batch calls) but still bounded
const DEFAULT_MAX_ACTIVATIONS = 3
const DEFAULT_DURATION_DAYS = 365
const TRIAL_DURATION_DAYS = 14

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
  return `${part()}-${part()}-${part()}-${part()}`
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

// ========== Admin auth ==========
// 管理接口必须使用独立的 ADMIN_API_KEY，不回退为 ACTIVATION_SECRET ——
// ACTIVATION_SECRET 泄露时不应连带交出管理后台。未配置时管理 API 整体停用。
// 另加每 IP 限流，减缓 Bearer 暴力破解（进程内 Map，实例重启后重置）。
const adminRateMap = new Map<string, { count: number; reset: number }>()

function checkAdminRateLimit(ip: string): boolean {
  const now = Math.floor(Date.now() / 1000)
  const entry = adminRateMap.get(ip)
  if (!entry || entry.reset < now) {
    adminRateMap.set(ip, { count: 1, reset: now + RATE_LIMIT_WINDOW })
    return true
  }
  if (entry.count >= ADMIN_RATE_LIMIT_MAX) return false
  entry.count++
  return true
}

function checkAdminAuth(c: Context<{ Bindings: Env }>): Response | null {
  const ip = c.req.header('CF-Connecting-IP') || 'unknown'
  if (!checkAdminRateLimit(ip)) {
    return c.json({ error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' }, 429)
  }
  if (!c.env.ADMIN_API_KEY) {
    return c.json({ error: 'ADMIN_NOT_CONFIGURED', message: '管理接口未配置（缺少 ADMIN_API_KEY）' }, 503)
  }
  const auth = c.req.header('Authorization')
  if (!auth || auth !== `Bearer ${c.env.ADMIN_API_KEY}`) {
    return c.json({ error: 'UNAUTHORIZED' }, 401)
  }
  return null
}

// ========== Ed25519 license helpers ==========

function getEd25519PrivateKey(env: Env): Uint8Array | null {
  if (!env.ED25519_PRIVATE_KEY) return null
  const hex = env.ED25519_PRIVATE_KEY.replace(/[^0-9a-fA-F]/g, '')
  if (hex.length !== 64) return null
  return hexToBytes(hex)
}

function canonicalLicensePayload(
  keyId: string,
  plan: string,
  deviceHash: string,
  deviceComponents: string[],
  issuedAt: number,
  expiresAt: number
): string {
  // Fixed field order matching the client-side reconstruction.
  return JSON.stringify({
    key_id: keyId,
    plan,
    device_hash: deviceHash,
    device_components: deviceComponents,
    issued_at: issuedAt,
    expires_at: expiresAt,
  })
}

function signWindowsLicense(payloadJson: string, privateKey: Uint8Array): string {
  const encoder = new TextEncoder()
  const message = encoder.encode(payloadJson)
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

  const payloadJson = canonicalLicensePayload(
    keyRow.key_id,
    keyRow.plan,
    deviceHash,
    deviceComponents,
    now,
    expiresAt
  )
  const signature = signWindowsLicense(payloadJson, privateKey)

  const license = {
    ...payload,
    signature,
  }

  return { license, payload }
}

// ========== Windows license issuance helper ==========

interface WindowsIssueSuccess {
  ok: true
  license: object
  plan: string
  expires_at: string | null
  device_hash: string
  activation_count: number
  max_activations: number
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
    max_activations: number | null
    activation_count: number | null
    duration_days: number | null
  }>()

  if (!keyRow) {
    return { ok: false, status: 404, body: { error: 'KEY_NOT_FOUND', message: '激活码不存在' } }
  }

  // 兼容旧状态值
  const status = keyRow.status === 'unused' ? 'available'
               : keyRow.status === 'used' ? 'activated'
               : keyRow.status

  if (status === 'revoked') {
    return { ok: false, status: 403, body: { error: 'KEY_REVOKED', message: '激活码已被吊销' } }
  }
  if (status === 'exhausted') {
    return { ok: false, status: 403, body: { error: 'EXHAUSTED', message: '该激活码已达到设备数上限（最多 3 台设备）' } }
  }

  // 兼容旧数据
  const maxActivations = keyRow.max_activations ?? DEFAULT_MAX_ACTIVATIONS
  const activationCount = keyRow.activation_count ?? 0
  const durationDays = keyRow.duration_days ?? (keyRow.plan === 'personal_yearly' ? DEFAULT_DURATION_DAYS : 0)

  const isSameDevice = keyRow.device_hash && keyRow.device_hash === deviceHash
  const isFirstActivation = !keyRow.device_hash

  // 检查过期（已有 expires_at 的情况）
  if (keyRow.expires_at) {
    const expiry = new Date(keyRow.expires_at)
    if (expiry < new Date()) {
      return { ok: false, status: 403, body: { error: 'SUBSCRIPTION_EXPIRED', message: '订阅已过期，请购买新的激活码' } }
    }
  }

  // 换设备检查
  if (!isSameDevice && !isFirstActivation) {
    if (activationCount >= maxActivations) {
      return {
        ok: false,
        status: 403,
        body: { error: 'EXHAUSTED', message: `该激活码最多激活 ${maxActivations} 台设备，已达上限` },
      }
    }
  }

  const privateKey = getEd25519PrivateKey(env)
  if (!privateKey) {
    return { ok: false, status: 500, body: { error: 'SERVER_ERROR', message: '签名密钥未配置' } }
  }

  const now = Math.floor(Date.now() / 1000)

  // 首次激活：设置 expires_at（从激活时刻起算）
  let expiresAt = 0
  let expiresAtIso: string | null = keyRow.expires_at
  if (!expiresAtIso && keyRow.plan === 'personal_yearly' && durationDays > 0) {
    const expiresDate = new Date(Date.now() + durationDays * 24 * 60 * 60 * 1000)
    expiresAt = Math.floor(expiresDate.getTime() / 1000)
    expiresAtIso = expiresDate.toISOString()
  } else if (expiresAtIso) {
    expiresAt = Math.floor(new Date(expiresAtIso).getTime() / 1000)
  }
  // personal_lifetime → expiresAt 保持 0

  const components = Array.isArray(deviceComponents) ? deviceComponents : []
  const payloadJson = canonicalLicensePayload(
    keyRow.key_id,
    keyRow.plan,
    deviceHash,
    components,
    now,
    expiresAt
  )
  const payload: WindowsLicensePayload = JSON.parse(payloadJson)
  let signature: string
  try {
    signature = signWindowsLicense(payloadJson, privateKey)
  } catch (e: any) {
    console.error('[signWindowsLicense] failed:', e?.message || e)
    return { ok: false, status: 500, body: { error: 'SERVER_ERROR', message: '签名失败: ' + (e?.message || String(e)) } }
  }
  const license = { ...payload, signature }

  // 计算新的激活次数
  // 计算新的激活次数：同设备重装不计数，换设备/首次激活 +1
  const newCount = isSameDevice ? activationCount : activationCount + 1
  const newStatus = newCount >= maxActivations ? 'exhausted' : 'activated'

  // Upsert device record
  try {
    await env.DB.prepare(`
      INSERT INTO windows_devices (device_id, key_id, device_name, activated_at, last_seen_at)
      VALUES (?, ?, ?, datetime('now'), datetime('now'))
      ON CONFLICT(device_id, key_id) DO UPDATE SET
        last_seen_at = datetime('now'),
        revoked = 0
    `).bind(deviceHash, keyId, deviceName || '').run()
  } catch (e: any) {
    console.error('[windows_devices] upsert failed:', e?.message || e)
    // Non-fatal: continue even if device upsert fails
  }

  // Update key binding
  try {
    await env.DB.prepare(`
      UPDATE windows_keys
      SET status = ?,
          device_hash = ?,
          device_components = ?,
          activated_at = COALESCE(activated_at, datetime('now')),
          activation_count = ?,
          max_activations = ?,
          expires_at = ?
      WHERE key_id = ?
    `).bind(newStatus, deviceHash, JSON.stringify(components), newCount, maxActivations, expiresAtIso, keyId).run()
  } catch (e: any) {
    console.error('[windows_keys] update failed:', e?.message || e)
    return { ok: false, status: 500, body: { error: 'SERVER_ERROR', message: '数据库更新失败: ' + (e?.message || String(e)) } }
  }

  return {
    ok: true,
    license,
    plan: keyRow.plan,
    expires_at: expiresAtIso,
    device_hash: deviceHash,
    activation_count: newCount,
    max_activations: maxActivations,
  }
}

interface WindowsTrialSuccess {
  ok: true
  license: object
  plan: string
  expires_at: string
  device_hash: string
}

type WindowsTrialResult = WindowsTrialSuccess | WindowsIssueFailure

async function issueWindowsTrial(
  env: Env,
  deviceHash: string,
  deviceName: string,
  deviceComponents: string[]
): Promise<WindowsTrialResult> {
  if (!deviceHash || deviceHash.length < 16 || deviceHash.length > 128) {
    return { ok: false, status: 400, body: { error: 'INVALID_DEVICE', message: '设备指纹无效' } }
  }

  const privateKey = getEd25519PrivateKey(env)
  if (!privateKey) {
    return { ok: false, status: 500, body: { error: 'TRIAL_UNAVAILABLE', message: '签名密钥未配置' } }
  }

  const now = Math.floor(Date.now() / 1000)
  const existing = await env.DB.prepare(
    'SELECT * FROM windows_trials WHERE device_hash = ?'
  ).bind(deviceHash).first<{
    device_hash: string
    key_id: string
    issued_at: string
    expires_at: string
  }>()

  let keyId: string
  let issuedAt: number
  let expiresAt: number

  if (existing) {
    expiresAt = Math.floor(new Date(existing.expires_at).getTime() / 1000)
    if (expiresAt <= now) {
      return { ok: false, status: 403, body: { error: 'TRIAL_EXPIRED', message: '这台设备的免费试用已结束' } }
    }
    keyId = existing.key_id
    issuedAt = Math.floor(new Date(existing.issued_at).getTime() / 1000)
    await env.DB.prepare(`
      UPDATE windows_trials
      SET device_name = ?,
          device_components = ?,
          last_seen_at = datetime('now')
      WHERE device_hash = ?
    `).bind(deviceName || '', JSON.stringify(deviceComponents || []), deviceHash).run()
  } else {
    keyId = `TRIAL-${deviceHash.slice(0, 12).toUpperCase()}`
    issuedAt = now
    expiresAt = now + TRIAL_DURATION_DAYS * 24 * 60 * 60
    await env.DB.prepare(`
      INSERT INTO windows_trials
        (device_hash, key_id, device_name, device_components, issued_at, expires_at, last_seen_at)
      VALUES (?, ?, ?, ?, ?, ?, datetime('now'))
    `).bind(
      deviceHash,
      keyId,
      deviceName || '',
      JSON.stringify(deviceComponents || []),
      new Date(issuedAt * 1000).toISOString(),
      new Date(expiresAt * 1000).toISOString()
    ).run()
  }

  const components = Array.isArray(deviceComponents) ? deviceComponents : []
  const payloadJson = canonicalLicensePayload(
    keyId,
    'trial',
    deviceHash,
    components,
    issuedAt,
    expiresAt
  )
  const payload: WindowsLicensePayload = JSON.parse(payloadJson)
  const signature = signWindowsLicense(payloadJson, privateKey)
  const license = { ...payload, signature }

  return {
    ok: true,
    license,
    plan: 'trial',
    expires_at: new Date(expiresAt * 1000).toISOString(),
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

function parseWindowsTrialBody(body: unknown): {
  device_hash: string
  device_name?: string
  device_components?: string[]
} | null {
  if (typeof body !== 'object' || body === null) return null
  const b = body as Record<string, unknown>
  if (typeof b.device_hash !== 'string') return null
  return {
    device_hash: b.device_hash,
    device_name: typeof b.device_name === 'string' ? b.device_name : undefined,
    device_components: Array.isArray(b.device_components)
      ? b.device_components.filter((x): x is string => typeof x === 'string')
      : undefined,
  }
}

// ========== App ==========

const app = new Hono<{ Bindings: Env }>()

// CORS
app.use('*', cors({
  origin: ['https://z-pulse.cn', 'https://xlsone.com', 'app://xlsone'],
  allowMethods: ['POST', 'GET', 'OPTIONS'],
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

// ========== POST /api/activate/windows ==========

app.post('/api/trial/windows', async (c) => {
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

  const parsed = parseWindowsTrialBody(body)
  if (!parsed) {
    return c.json({ error: 'INVALID_PARAMS', message: '请求参数无效' }, 400)
  }

  const result = await issueWindowsTrial(
    c.env,
    parsed.device_hash,
    parsed.device_name || '',
    parsed.device_components || []
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
    activation_count: result.activation_count,
    max_activations: result.max_activations,
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

// ========== POST /api/admin/generate-windows-keys ==========

app.post('/api/admin/generate-windows-keys', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  let body: { count?: number; plan?: string; max_activations?: number; duration_days?: number }
  try {
    body = await c.req.json()
  } catch {
    return c.json({ error: 'INVALID_JSON' }, 400)
  }

  const count = Math.min(body.count || 1, 100)
  const plan = body.plan || 'personal_yearly'

  // Plan-specific defaults
  let maxActivations = DEFAULT_MAX_ACTIVATIONS
  let durationDays = 0
  if (plan === 'trial') {
    maxActivations = 1
    durationDays = 14
  } else if (plan === 'personal_yearly') {
    maxActivations = 3
    durationDays = 365
  } else if (plan === 'personal_lifetime') {
    maxActivations = 3
    durationDays = 0
  }

  // Allow explicit override
  if (body.max_activations !== undefined) maxActivations = body.max_activations
  if (body.duration_days !== undefined) durationDays = body.duration_days

  const keys: string[] = []
  const stmt = c.env.DB.prepare(
    'INSERT INTO windows_keys (key_id, plan, status, max_activations, activation_count, duration_days) VALUES (?, ?, ?, ?, ?, ?)'
  )

  const batch = []
  for (let i = 0; i < count; i++) {
    const keyId = generateKeyId()
    batch.push(stmt.bind(keyId, plan, 'available', maxActivations, 0, durationDays))
    keys.push(keyId)
  }

  await c.env.DB.batch(batch)

  return c.json({ keys, count, plan, max_activations: maxActivations, duration_days: durationDays })
})

// ========== GET /api/admin/windows-keys (码列表) ==========

app.get('/api/admin/windows-keys', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const status = c.req.query('status') || ''
  const plan = c.req.query('plan') || ''
  const search = c.req.query('search') || ''
  const page = Math.max(parseInt(c.req.query('page') || '1', 10), 1)
  const limit = Math.min(Math.max(parseInt(c.req.query('limit') || '20', 10), 1), 100)

  let sql = 'SELECT * FROM windows_keys WHERE 1=1'
  const params: (string | number)[] = []

  if (status) {
    sql += ' AND status = ?'
    params.push(status)
  }
  if (plan) {
    sql += ' AND plan = ?'
    params.push(plan)
  }
  if (search) {
    sql += ' AND (key_id LIKE ? OR order_id LIKE ? OR email LIKE ?)'
    const pattern = `%${search}%`
    params.push(pattern, pattern, pattern)
  }

  sql += ' ORDER BY created_at DESC LIMIT ? OFFSET ?'
  params.push(limit, (page - 1) * limit)

  const rows = await c.env.DB.prepare(sql).bind(...params).all<Record<string, any>>()

  const countSql = 'SELECT COUNT(*) as total FROM windows_keys WHERE 1=1'
  const countParams: (string | number)[] = []
  let countSqlStr = countSql
  if (status) { countSqlStr += ' AND status = ?'; countParams.push(status) }
  if (plan) { countSqlStr += ' AND plan = ?'; countParams.push(plan) }
  if (search) { countSqlStr += ' AND (key_id LIKE ? OR order_id LIKE ? OR email LIKE ?)'; countParams.push(`%${search}%`, `%${search}%`, `%${search}%`) }

  const countRow = await c.env.DB.prepare(countSqlStr).bind(...countParams).first<{ total: number }>()
  const total = countRow?.total || 0

  return c.json({
    keys: rows.results || [],
    pagination: { page, limit, total, pages: Math.ceil(total / limit) }
  })
})

// ========== GET /api/admin/windows-keys/:key_id (码详情) ==========

app.get('/api/admin/windows-keys/:key_id', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const keyId = c.req.param('key_id').toUpperCase()
  const keyRow = await c.env.DB.prepare(
    'SELECT * FROM windows_keys WHERE key_id = ?'
  ).bind(keyId).first<Record<string, any>>()

  if (!keyRow) return c.json({ error: 'KEY_NOT_FOUND' }, 404)

  const devices = await c.env.DB.prepare(
    'SELECT * FROM windows_devices WHERE key_id = ? ORDER BY activated_at DESC'
  ).bind(keyId).all<Record<string, any>>()

  return c.json({ key: keyRow, devices: devices.results || [] })
})

// ========== POST /api/admin/windows-keys/:key_id/revoke (吊销码) ==========

app.post('/api/admin/windows-keys/:key_id/revoke', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const keyId = c.req.param('key_id').toUpperCase()
  const keyRow = await c.env.DB.prepare(
    'SELECT status FROM windows_keys WHERE key_id = ?'
  ).bind(keyId).first<{ status: string }>()

  if (!keyRow) return c.json({ error: 'KEY_NOT_FOUND' }, 404)
  if (keyRow.status === 'revoked') return c.json({ error: 'ALREADY_REVOKED' }, 400)

  await c.env.DB.prepare(
    "UPDATE windows_keys SET status = 'revoked', revoked_at = datetime('now') WHERE key_id = ?"
  ).bind(keyId).run()

  return c.json({ key_id: keyId, status: 'revoked' })
})

// ========== POST /api/admin/windows-keys/:key_id/restore (恢复码) ==========

app.post('/api/admin/windows-keys/:key_id/restore', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const keyId = c.req.param('key_id').toUpperCase()
  const keyRow = await c.env.DB.prepare(
    'SELECT status, activation_count, max_activations FROM windows_keys WHERE key_id = ?'
  ).bind(keyId).first<{ status: string; activation_count: number; max_activations: number }>()

  if (!keyRow) return c.json({ error: 'KEY_NOT_FOUND' }, 404)
  if (keyRow.status !== 'revoked') return c.json({ error: 'NOT_REVOKED' }, 400)

  const newStatus = (keyRow.activation_count || 0) >= (keyRow.max_activations || DEFAULT_MAX_ACTIVATIONS) ? 'exhausted' : 'available'

  await c.env.DB.prepare(
    "UPDATE windows_keys SET status = ?, revoked_at = NULL WHERE key_id = ?"
  ).bind(newStatus, keyId).run()

  return c.json({ key_id: keyId, status: newStatus })
})

// ========== GET /api/admin/windows-devices (设备列表) ==========

app.get('/api/admin/windows-devices', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const keyId = (c.req.query('key_id') || '').toUpperCase()
  const search = c.req.query('search') || ''
  const revoked = c.req.query('revoked')
  const page = Math.max(parseInt(c.req.query('page') || '1', 10), 1)
  const limit = Math.min(Math.max(parseInt(c.req.query('limit') || '20', 10), 1), 100)

  let sql = 'SELECT * FROM windows_devices WHERE 1=1'
  const params: (string | number)[] = []

  if (keyId) {
    sql += ' AND key_id = ?'
    params.push(keyId)
  }
  if (revoked !== undefined && revoked !== '') {
    sql += ' AND revoked = ?'
    params.push(parseInt(revoked, 10))
  }
  if (search) {
    sql += ' AND (device_id LIKE ? OR device_name LIKE ?)'
    const pattern = `%${search}%`
    params.push(pattern, pattern)
  }

  sql += ' ORDER BY activated_at DESC LIMIT ? OFFSET ?'
  params.push(limit, (page - 1) * limit)

  const rows = await c.env.DB.prepare(sql).bind(...params).all<Record<string, any>>()

  let countSql = 'SELECT COUNT(*) as total FROM windows_devices WHERE 1=1'
  const countParams: (string | number)[] = []
  if (keyId) { countSql += ' AND key_id = ?'; countParams.push(keyId) }
  if (revoked !== undefined && revoked !== '') { countSql += ' AND revoked = ?'; countParams.push(parseInt(revoked, 10)) }
  if (search) { countSql += ' AND (device_id LIKE ? OR device_name LIKE ?)'; countParams.push(`%${search}%`, `%${search}%`) }

  const countRow = await c.env.DB.prepare(countSql).bind(...countParams).first<{ total: number }>()
  const total = countRow?.total || 0

  return c.json({
    devices: rows.results || [],
    pagination: { page, limit, total, pages: Math.ceil(total / limit) }
  })
})

// ========== POST /api/admin/windows-devices/:device_id/revoke (解绑设备) ==========

app.post('/api/admin/windows-devices/:device_id/revoke', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const deviceId = c.req.param('device_id')
  const device = await c.env.DB.prepare(
    'SELECT device_id, key_id, revoked FROM windows_devices WHERE device_id = ?'
  ).bind(deviceId).first<{ device_id: string; key_id: string; revoked: number }>()

  if (!device) return c.json({ error: 'DEVICE_NOT_FOUND' }, 404)
  if (device.revoked) return c.json({ error: 'ALREADY_REVOKED' }, 400)

  await c.env.DB.prepare(
    "UPDATE windows_devices SET revoked = 1, revoked_at = datetime('now') WHERE device_id = ?"
  ).bind(deviceId).run()

  const keyRow = await c.env.DB.prepare(
    'SELECT activation_count, status FROM windows_keys WHERE key_id = ?'
  ).bind(device.key_id).first<{ activation_count: number; status: string }>()

  if (keyRow && keyRow.activation_count > 0) {
    const newCount = keyRow.activation_count - 1
    const newStatus = keyRow.status === 'exhausted' ? 'activated' : keyRow.status
    await c.env.DB.prepare(
      'UPDATE windows_keys SET activation_count = ?, status = ? WHERE key_id = ?'
    ).bind(newCount, newStatus, device.key_id).run()
  }

  return c.json({ device_id: deviceId, revoked: true })
})

// ========== GET /api/admin/pool-stats ==========

app.get('/api/admin/pool-stats', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

  const rows = await c.env.DB.prepare(
    "SELECT status FROM windows_keys"
  ).all<{ status: string }>()

  const stats = { available: 0, activated: 0, exhausted: 0, revoked: 0, total: 0 }
  for (const row of rows.results || []) {
    const s = row.status === 'unused' ? 'available' : row.status === 'used' ? 'activated' : row.status
    if (stats[s as keyof typeof stats] !== undefined) {
      (stats as any)[s]++
    }
    stats.total++
  }

  return c.json(stats)
})

// ========== POST /api/admin/generate (管理后台生成激活码) ==========

app.post('/api/admin/generate', async (c) => {
  const adminError = checkAdminAuth(c)
  if (adminError) return adminError

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
