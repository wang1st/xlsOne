'use strict';
/*
 * xlsOne 国内激活 API —— 部署在 z-pulse.cn（中国大陆节点）
 *
 * 与 Cloudflare Worker (activation/worker/src/index.ts) 功能等价，但运行在普通
 * Linux 服务器（Node.js）上，使用 JSON 文件存储代替 D1，使用 Node 内置 WebCrypto
 * 代替 Web Crypto / btoa / atob。Ed25519 签名与客户端 license_manager.cpp 中固化
 * 的公钥共用同一私钥种子（ED25519_PRIVATE_KEY），因此签发的 Windows 授权文件可被
 * 客户端验过。
 *
 * 依赖：仅 Node.js 内置模块（http / crypto / fs / path）。无需 npm install。
 *
 * 路由：
 *   GET  /api/health
 *   POST /api/activate                    旧版 HMAC/JWT 激活（macOS 在线）
 *   POST /api/verify                      验证 License Token
 *   POST /api/refresh                     刷新订阅 Token
 *   POST /api/trial/windows               Windows 版签名试用许可证
 *   POST /api/activate/windows            Windows 版 Ed25519 许可证激活
 *   POST /api/license/download             离线激活：返回签名的 .license 文件
 *   POST /api/admin/generate              管理后台生成激活码（macOS）
 *   POST /api/admin/generate-windows-keys 管理后台批量生成 Windows 激活码
 *   GET  /api/admin/pool-stats            激活码池统计（available/activated/exhausted/revoked）
 *   GET  /api/admin/windows-keys           Windows 激活码列表查询（分页、筛选、搜索）
 *   GET  /api/admin/windows-keys/:id        Windows 激活码详情（含设备列表）
 *   GET  /api/admin/windows-keys/export     导出所有可用激活码文本列表（每行一个）
 *   POST /api/admin/windows-devices/:id/revoke 解绑 Windows 设备
 *   GET  ${ADMIN_PATH}                   授权码管理系统（管理页面，由 ADMIN_PATH 配置，默认 /xlsone/license-console）
 *   GET  /offline /xlsone/offline         离线激活网页
 *   GET  /downloads / /downloads/*         安装包下载页 / 静态文件
 *   GET  /                                状态页
 */

const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

// ========== Config ==========
const PORT = parseInt(process.env.PORT || '8787', 10);
const DATA_DIR = process.env.DATA_DIR || path.join(__dirname, 'data');
const DB_PATH = process.env.DB_PATH || path.join(DATA_DIR, 'store.json');
const PUBLIC_DIR = path.join(__dirname, 'public');
const ED25519_PRIVATE_KEY = process.env.ED25519_PRIVATE_KEY || '';
const ACTIVATION_SECRET = process.env.ACTIVATION_SECRET || '';
// 管理接口密钥必须独立配置，不回退为 ACTIVATION_SECRET：
// ACTIVATION_SECRET 一旦泄露不应连带交出管理后台。未配置时管理 API 整体停用。
const ADMIN_API_KEY = process.env.ADMIN_API_KEY || '';
const DOWNLOADS_DIR = process.env.DOWNLOADS_DIR || path.join(DATA_DIR, 'downloads');
const ADMIN_PATH = process.env.ADMIN_PATH || '/xlsone/license-console';

// ========== Constants ==========
const KEY_PATTERN = /^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$/;
const WINDOWS_KEY_PATTERN = /^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$/;
const TOKEN_LIFETIME = 30 * 24 * 60 * 60;
const DEFAULT_MAX_ACTIVATIONS = 3;
const DEFAULT_DURATION_DAYS = 365;
const TRIAL_DURATION_DAYS = 14;

// ========== JSON store ==========
function ensureDir(p) { if (!fs.existsSync(p)) fs.mkdirSync(p, { recursive: true }); }
ensureDir(DATA_DIR);
ensureDir(DOWNLOADS_DIR);

const DEFAULT_STORE = {
  activation_keys: {},   // key_id -> { key_id, product, plan, status, max_devices, expires_at, order_id, email, used_at }
  devices: {},           // device_id -> { device_id, key_id, license_token, device_name, activated_at, last_seen_at, revoked }
  windows_keys: {},      // key_id -> { key_id, plan, status, device_hash, device_components, expires_at, order_id, email, activated_at, max_activations, activation_count, duration_days }
  windows_devices: {},   // device_id -> { device_id, key_id, device_name, activated_at, last_seen_at, revoked }
  windows_trials: {},    // device_hash -> { device_hash, key_id, device_name, device_components, issued_at, expires_at, last_seen_at }
  windows_migrations: [],// { key_id, old_device, new_device, migrated_at }
  orders: {},            // order_id -> { order_id, key_id, status, created_at }
};

let store = loadStore();
let storeWriteTimer = null;
function loadStore() {
  try {
    if (fs.existsSync(DB_PATH)) {
      const raw = JSON.parse(fs.readFileSync(DB_PATH, 'utf8'));
      return Object.assign({}, DEFAULT_STORE, raw);
    }
  } catch (e) {
    console.error('[store] load failed, starting fresh:', e.message);
  }
  return JSON.parse(JSON.stringify(DEFAULT_STORE));
}
function saveStoreSoon() {
  if (storeWriteTimer) return;
  storeWriteTimer = setTimeout(() => {
    storeWriteTimer = null;
    try {
      fs.writeFileSync(DB_PATH, JSON.stringify(store, null, 2));
    } catch (e) {
      console.error('[store] write failed:', e.message);
    }
  }, 200);
}

// ========== Helpers ==========
function b64urlEncode(input) {
  const buf = Buffer.isBuffer(input) ? input : Buffer.from(input, 'utf8');
  return buf.toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
function b64urlDecode(str) {
  str = String(str).replace(/-/g, '+').replace(/_/g, '/');
  while (str.length % 4) str += '=';
  return Buffer.from(str, 'base64');
}
function md5(str) {
  return crypto.createHash('md5').update(str, 'utf8').digest('hex');
}
function hmacSign(secret, data) {
  return crypto.createHmac('sha256', secret).update(data, 'utf8').digest('hex');
}
function timingSafeEqual(a, b) {
  const ba = Buffer.from(a);
  const bb = Buffer.from(b);
  if (ba.length !== bb.length) return false;
  return crypto.timingSafeEqual(ba, bb);
}

// ----- Ed25519 -----
// Node's WebCrypto cannot import a raw 32-byte Ed25519 seed (usage bug), so we
// load the key from a PKCS#8 PEM instead. The PEM is generated from the seed
// once (at deploy time, via `keygen.py` using Python `cryptography`), cached at
// data/ed25519.pem, and reused. If absent, we generate it on first start.
const ED25519_PEM_PATH = process.env.ED25519_PEM_PATH || path.join(DATA_DIR, 'ed25519.pem');
let edPrivateKey = null;

function ensureEd25519Key() {
  if (edPrivateKey) return edPrivateKey;
  if (!ED25519_PRIVATE_KEY || ED25519_PRIVATE_KEY.replace(/[^0-9a-fA-F]/g, '').length !== 64) {
    throw new Error('ED25519_PRIVATE_KEY must be a 64-char hex seed');
  }
  if (!fs.existsSync(ED25519_PEM_PATH)) {
    console.log('[ed25519] PEM not found, generating from seed via keygen.py ...');
    const r = spawnSync(process.env.PYTHON_BIN || 'python3', [path.join(__dirname, 'keygen.py')], {
      input: '',
      env: Object.assign({}, process.env, { SEED_HEX: ED25519_PRIVATE_KEY.replace(/[^0-9a-fA-F]/g, '') }),
      maxBuffer: 1 << 20,
    });
    if (r.status !== 0 || !r.stdout) {
      throw new Error('Failed to generate Ed25519 PEM: ' + (r.stderr ? r.stderr.toString() : 'no output'));
    }
    fs.writeFileSync(ED25519_PEM_PATH, r.stdout);
  }
  edPrivateKey = crypto.createPrivateKey(fs.readFileSync(ED25519_PEM_PATH, 'utf8'));
  return edPrivateKey;
}

function canonicalLicensePayload(keyId, plan, deviceHash, deviceComponents, issuedAt, expiresAt) {
  // Fixed field order matching the client-side reconstruction.
  return JSON.stringify({
    key_id: keyId,
    plan: plan,
    device_hash: deviceHash,
    device_components: deviceComponents,
    issued_at: issuedAt,
    expires_at: expiresAt,
  });
}

function signWindowsLicenseSync(payloadJson, privateKey) {
  const message = Buffer.from(payloadJson, 'utf8');
  const sig = crypto.sign(null, message, privateKey);
  return b64urlEncode(sig);
}

// ----- JWT (HMAC-SHA256) -----
function signJWT(payload, secret) {
  const header = b64urlEncode(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
  const body = b64urlEncode(JSON.stringify(payload));
  const sig = b64urlEncode(crypto.createHmac('sha256', secret).update(`${header}.${body}`).digest());
  return `${header}.${body}.${sig}`;
}
function verifyJWT(token, secret) {
  try {
    const parts = String(token).split('.');
    if (parts.length !== 3) return null;
    const expectedSig = b64urlEncode(crypto.createHmac('sha256', secret).update(`${parts[0]}.${parts[1]}`).digest());
    if (!timingSafeEqual(expectedSig, parts[2])) return null;
    const payload = JSON.parse(b64urlDecode(parts[1]).toString('utf8'));
    return payload;
  } catch {
    return null;
  }
}

function generateWindowsKeyId() {
  const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  const part = () => Array.from({ length: 4 }, () => chars[Math.floor(Math.random() * chars.length)]).join('');
  return `${part()}-${part()}-${part()}-${part()}`;
}

// In-memory rate limiter (per client IP)
const rateMap = new Map();
function checkRateLimit(ip) {
  const now = Math.floor(Date.now() / 1000);
  const entry = rateMap.get(ip);
  if (!entry || entry.reset < now) {
    rateMap.set(ip, { count: 1, reset: now + 60 });
    return true;
  }
  if (entry.count >= 10) return false;
  entry.count++;
  return true;
}

// 管理接口限流更宽松（管理页面一次加载多个接口），但仍限制暴力破解 Bearer。
const adminRateMap = new Map();
function checkAdminRateLimit(ip) {
  const now = Math.floor(Date.now() / 1000);
  const entry = adminRateMap.get(ip);
  if (!entry || entry.reset < now) {
    adminRateMap.set(ip, { count: 1, reset: now + 60 });
    return true;
  }
  if (entry.count >= 30) return false;
  entry.count++;
  return true;
}

// 管理接口统一鉴权：限流 + 独立密钥 + 常量时间比较。
// 通过返回 true；否则已写入响应并返回 false。
function requireAdmin(req, res) {
  const ip = clientIp(req);
  if (!checkAdminRateLimit(ip)) {
    sendJson(res, 429, { error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' });
    return false;
  }
  if (!ADMIN_API_KEY) {
    sendJson(res, 503, { error: 'ADMIN_NOT_CONFIGURED', message: '管理接口未配置（缺少 ADMIN_API_KEY）' });
    return false;
  }
  const auth = String(req.headers['authorization'] || '');
  const expected = `Bearer ${ADMIN_API_KEY}`;
  if (auth.length !== expected.length || !timingSafeEqual(auth, expected)) {
    sendJson(res, 401, { error: 'UNAUTHORIZED' });
    return false;
  }
  return true;
}

// ========== License issuance (Windows) ==========
async function issueWindowsLicense(keyId, deviceHash, deviceName, deviceComponents) {
  const keyRow = store.windows_keys[keyId];
  if (!keyRow) return { ok: false, status: 404, body: { error: 'KEY_NOT_FOUND', message: '激活码不存在' } };

  // 兼容旧状态值：unused → available, used → activated
  const status = keyRow.status === 'unused' ? 'available'
               : keyRow.status === 'used' ? 'activated'
               : keyRow.status;

  if (status === 'revoked') return { ok: false, status: 403, body: { error: 'KEY_REVOKED', message: '激活码已被吊销' } };
  if (status === 'exhausted') return { ok: false, status: 403, body: { error: 'EXHAUSTED', message: '该激活码已达到设备数上限（最多 3 台设备）' } };

  // 兼容旧数据
  const maxActivations = keyRow.max_activations ?? DEFAULT_MAX_ACTIVATIONS;
  const activationCount = keyRow.activation_count ?? 0;
  const durationDays = keyRow.duration_days ?? (keyRow.plan === 'personal_yearly' ? DEFAULT_DURATION_DAYS : 0);

  const isSameDevice = keyRow.device_hash && keyRow.device_hash === deviceHash;
  const isFirstActivation = !keyRow.device_hash;

  // 检查过期（已有 expires_at 的情况——码已被激活过）
  if (keyRow.expires_at) {
    const expiry = new Date(keyRow.expires_at);
    if (expiry < new Date()) {
      return { ok: false, status: 403, body: { error: 'SUBSCRIPTION_EXPIRED', message: '订阅已过期，请购买新的激活码' } };
    }
  }

  // 换设备检查（不同设备且非首次激活）
  if (!isSameDevice && !isFirstActivation) {
    if (activationCount >= maxActivations) {
      return { ok: false, status: 403, body: { error: 'EXHAUSTED', message: `该激活码最多激活 ${maxActivations} 台设备，已达上限` } };
    }
  }

  let privateKey;
  try {
    privateKey = ensureEd25519Key();
  } catch (e) {
    return { ok: false, status: 500, body: { error: 'SERVER_ERROR', message: '签名密钥未配置: ' + e.message } };
  }

  const now = Math.floor(Date.now() / 1000);

  // 首次激活：设置 expires_at（从激活时刻起算，而非生成时刻）
  let expiresAt = 0;
  if (keyRow.expires_at) {
    expiresAt = Math.floor(new Date(keyRow.expires_at).getTime() / 1000);
  } else if (keyRow.plan === 'personal_yearly' && durationDays > 0) {
    const expiresDate = new Date(Date.now() + durationDays * 24 * 60 * 60 * 1000);
    expiresAt = Math.floor(expiresDate.getTime() / 1000);
    keyRow.expires_at = expiresDate.toISOString();
  }
  // personal_lifetime → expiresAt 保持 0（永久）

  const payloadJson = canonicalLicensePayload(
    keyRow.key_id,
    keyRow.plan,
    deviceHash,
    deviceComponents,
    now,
    expiresAt
  );
  const signature = signWindowsLicenseSync(payloadJson, privateKey);
  const payload = JSON.parse(payloadJson);
  const license = Object.assign({}, payload, { signature });

  // 计算新的激活次数：同设备重装不计数，换设备/首次激活 +1
  const newCount = isSameDevice ? activationCount : activationCount + 1;

  // Upsert device
  store.windows_devices[deviceHash] = {
    device_id: deviceHash,
    key_id: keyId,
    device_name: deviceName || '',
    activated_at: new Date().toISOString(),
    last_seen_at: new Date().toISOString(),
    revoked: 0,
  };

  // 更新 key 记录
  const newStatus = newCount >= maxActivations ? 'exhausted' : 'activated';
  store.windows_keys[keyId] = Object.assign({}, keyRow, {
    status: newStatus,
    device_hash: deviceHash,
    device_components: JSON.stringify(deviceComponents),
    activated_at: keyRow.activated_at || new Date().toISOString(),
    activation_count: newCount,
    max_activations: maxActivations,
    expires_at: keyRow.expires_at,
  });

  saveStoreSoon();

  return {
    ok: true,
    license,
    plan: keyRow.plan,
    expires_at: keyRow.expires_at,
    device_hash: deviceHash,
    activation_count: newCount,
    max_activations: maxActivations,
  };
}

async function issueWindowsTrial(deviceHash, deviceName, deviceComponents) {
  if (!deviceHash || deviceHash.length < 16 || deviceHash.length > 128) {
    return { ok: false, status: 400, body: { error: 'INVALID_DEVICE', message: '设备指纹无效' } };
  }

  let privateKey;
  try {
    privateKey = ensureEd25519Key();
  } catch (e) {
    return { ok: false, status: 500, body: { error: 'TRIAL_UNAVAILABLE', message: '签名密钥未配置: ' + e.message } };
  }

  const now = Math.floor(Date.now() / 1000);
  const existing = store.windows_trials[deviceHash];
  let keyId;
  let issuedAt;
  let expiresAt;

  if (existing) {
    expiresAt = Math.floor(new Date(existing.expires_at).getTime() / 1000);
    if (expiresAt <= now) {
      return { ok: false, status: 403, body: { error: 'TRIAL_EXPIRED', message: '这台设备的免费试用已结束' } };
    }
    keyId = existing.key_id;
    issuedAt = Math.floor(new Date(existing.issued_at).getTime() / 1000);
    existing.last_seen_at = new Date().toISOString();
    existing.device_name = deviceName || existing.device_name || '';
    existing.device_components = JSON.stringify(deviceComponents || []);
  } else {
    keyId = `TRIAL-${deviceHash.slice(0, 12).toUpperCase()}`;
    issuedAt = now;
    expiresAt = now + TRIAL_DURATION_DAYS * 24 * 60 * 60;
    store.windows_trials[deviceHash] = {
      device_hash: deviceHash,
      key_id: keyId,
      device_name: deviceName || '',
      device_components: JSON.stringify(deviceComponents || []),
      issued_at: new Date(issuedAt * 1000).toISOString(),
      expires_at: new Date(expiresAt * 1000).toISOString(),
      last_seen_at: new Date().toISOString(),
    };
  }

  const components = Array.isArray(deviceComponents) ? deviceComponents : [];
  const payloadJson = canonicalLicensePayload(
    keyId,
    'trial',
    deviceHash,
    components,
    issuedAt,
    expiresAt
  );
  const signature = signWindowsLicenseSync(payloadJson, privateKey);
  const payload = JSON.parse(payloadJson);
  const license = Object.assign({}, payload, { signature });
  saveStoreSoon();

  return {
    ok: true,
    license,
    plan: 'trial',
    expires_at: new Date(expiresAt * 1000).toISOString(),
    device_hash: deviceHash,
  };
}

function parseWindowsBody(body) {
  if (typeof body !== 'object' || body === null) return null;
  if (typeof body.key !== 'string' || typeof body.device_hash !== 'string') return null;
  return {
    key: body.key,
    device_hash: body.device_hash,
    device_name: typeof body.device_name === 'string' ? body.device_name : '',
    device_components: Array.isArray(body.device_components)
      ? body.device_components.filter((x) => typeof x === 'string')
      : [],
  };
}

function parseWindowsTrialBody(body) {
  if (typeof body !== 'object' || body === null) return null;
  if (typeof body.device_hash !== 'string') return null;
  return {
    device_hash: body.device_hash,
    device_name: typeof body.device_name === 'string' ? body.device_name : '',
    device_components: Array.isArray(body.device_components)
      ? body.device_components.filter((x) => typeof x === 'string')
      : [],
  };
}

// ========== HTTP utilities ==========
function sendJson(res, status, obj) {
  const data = Buffer.from(JSON.stringify(obj), 'utf8');
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'POST, GET, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type, Authorization',
    'Cache-Control': 'no-store',
  });
  res.end(data);
}
function sendFile(res, filePath, contentType, downloadName) {
  if (!fs.existsSync(filePath)) { sendJson(res, 404, { error: 'NOT_FOUND' }); return; }
  const data = fs.readFileSync(filePath);
  const headers = { 'Content-Type': contentType || 'application/octet-stream', 'Access-Control-Allow-Origin': '*' };
  if (downloadName) headers['Content-Disposition'] = `attachment; filename="${downloadName}"`;
  res.writeHead(200, headers);
  res.end(data);
}
function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (c) => { data += c; if (data.length > 1e6) req.destroy(); });
    req.on('end', () => resolve(data));
    req.on('error', reject);
  });
}
function clientIp(req) {
  // 部署在 nginx 反代之后：X-Real-IP 由 nginx 用真实对端地址覆盖写入，可信；
  // X-Forwarded-For 使用 $proxy_add_x_forwarded_for 追加模式，客户端可伪造
  // 前面的条目，只有最右一跳（nginx 看到的对端）可信。
  const xr = req.headers['x-real-ip'];
  if (xr) return String(xr).trim();
  const xff = req.headers['x-forwarded-for'];
  if (xff) {
    const parts = String(xff).split(',');
    return parts[parts.length - 1].trim();
  }
  return req.socket.remoteAddress || 'unknown';
}

// ========== Request router ==========
const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    const p = url.pathname;
    const method = req.method;

    if (method === 'OPTIONS') { sendJson(res, 204, {}); return; }

    // ---- Static / page routes ----
    if (method === 'GET' && (p === '/' || p === '/api')) {
      return sendJson(res, 200, { service: 'xlsone-activation-api', region: 'domestic', version: '1.0.0' });
    }
    if (method === 'GET' && p === '/api/health') {
      return sendJson(res, 200, { status: 'ok', timestamp: new Date().toISOString() });
    }
    if (method === 'GET' && (p === '/offline' || p === '/xlsone/offline')) {
      return sendFile(res, path.join(PUBLIC_DIR, 'offline.html'), 'text/html; charset=utf-8');
    }
    if (method === 'GET' && p === ADMIN_PATH) {
      const adminHtml = path.join(__dirname, 'public', 'license-manager.html');
      return sendFile(res, adminHtml, 'text/html; charset=utf-8');
    }
    if (method === 'GET' && p === '/downloads') {
      // Trailing-slash redirect so the page's relative "./file" links resolve
      // correctly, regardless of which prefix nginx proxies it under
      // (e.g. /activation/downloads/ on z-pulse.cn or /downloads/ on api.z-pulse.cn).
      res.writeHead(301, { Location: '/downloads/' });
      return res.end();
    }
    if (method === 'GET' && p === '/downloads/') {
      return sendFile(res, path.join(PUBLIC_DIR, 'downloads.html'), 'text/html; charset=utf-8');
    }
    if (method === 'GET' && p === '/api/downloads') {
      let files = [];
      try {
        files = fs.readdirSync(DOWNLOADS_DIR)
          .filter((f) => fs.statSync(path.join(DOWNLOADS_DIR, f)).isFile())
          .map((f) => {
            const st = fs.statSync(path.join(DOWNLOADS_DIR, f));
            return { name: f, size: st.size, mtime: st.mtimeMs };
          })
          .sort((a, b) => b.mtime - a.mtime);
      } catch {}
      return sendJson(res, 200, { files, base: '/downloads/' });
    }
    if (method === 'GET' && p.startsWith('/downloads/')) {
      const fname = path.basename(p);
      return sendFile(res, path.join(DOWNLOADS_DIR, fname), null, fname);
    }

    // ---- API: version (update check) ----
    if (method === 'GET' && (p === '/api/version' || p === '/api/version.json')) {
      const versionJsonPath = path.join(__dirname, 'site', 'api', 'version.json');
      if (fs.existsSync(versionJsonPath)) {
        return sendFile(res, versionJsonPath, 'application/json; charset=utf-8');
      }
      return sendJson(res, 404, { error: 'NOT_FOUND', message: '版本信息文件未找到' });
    }

    // ---- API: health ----
    if (method === 'GET' && p === '/api/health') {
      return sendJson(res, 200, { status: 'ok' });
    }

    // ---- API: macOS activate (HMAC JWT) ----
    if (method === 'POST' && p === '/api/activate') {
      const ip = clientIp(req);
      if (!checkRateLimit(ip)) return sendJson(res, 429, { error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' });
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const { key, device_id, device_name } = body || {};
      if (!key || !KEY_PATTERN.test(String(key).toUpperCase())) return sendJson(res, 400, { error: 'INVALID_KEY', message: '激活码格式不正确' });
      if (!device_id || device_id.length < 8 || device_id.length > 128) return sendJson(res, 400, { error: 'INVALID_DEVICE', message: '设备标识无效' });
      const keyId = String(key).toUpperCase();
      const keyRow = store.activation_keys[keyId];
      if (!keyRow) return sendJson(res, 404, { error: 'KEY_NOT_FOUND', message: '激活码不存在' });
      if (keyRow.status === 'revoked') return sendJson(res, 403, { error: 'KEY_REVOKED', message: '激活码已被吊销' });
      const devCount = Object.values(store.devices).filter((d) => d.key_id === keyId && !d.revoked).length;
      if (devCount >= keyRow.max_devices) {
        const existing = store.devices[device_id];
        if (!existing || existing.key_id !== keyId || existing.revoked) {
          return sendJson(res, 403, { error: 'DEVICE_LIMIT', message: `该激活码已达到最大设备数限制（${keyRow.max_devices}台）`, current_devices: devCount, max_devices: keyRow.max_devices });
        }
      }
      if (keyRow.expires_at) {
        if (new Date(keyRow.expires_at) < new Date()) return sendJson(res, 403, { error: 'SUBSCRIPTION_EXPIRED', message: '订阅已过期' });
      }
      const now = Math.floor(Date.now() / 1000);
      let exp = 0;
      if (keyRow.expires_at) exp = Math.min(Math.floor(new Date(keyRow.expires_at).getTime() / 1000), now + TOKEN_LIFETIME);
      const payload = { sub: keyId, dev: device_id, plan: keyRow.plan, iat: now, exp };
      const token = signJWT(payload, ACTIVATION_SECRET);
      store.devices[device_id] = {
        device_id, key_id: keyId, license_token: token, device_name: device_name || '',
        activated_at: new Date().toISOString(), last_seen_at: new Date().toISOString(), revoked: 0,
      };
      if (keyRow.status === 'unused') { keyRow.status = 'used'; keyRow.used_at = new Date().toISOString(); }
      saveStoreSoon();
      return sendJson(res, 200, { license_token: token, plan: keyRow.plan, expires_at: keyRow.expires_at || null, device_id });
    }

    // ---- API: macOS verify ----
    if (method === 'POST' && p === '/api/verify') {
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const { token, device_id } = body || {};
      if (!token || !device_id) return sendJson(res, 400, { error: 'MISSING_PARAMS' });
      const payload = verifyJWT(token, ACTIVATION_SECRET);
      if (!payload) return sendJson(res, 401, { error: 'INVALID_TOKEN', message: '许可证无效' });
      if (payload.dev !== device_id) return sendJson(res, 403, { error: 'DEVICE_MISMATCH', message: '设备不匹配' });
      if (payload.exp > 0 && payload.exp < Math.floor(Date.now() / 1000)) return sendJson(res, 403, { error: 'TOKEN_EXPIRED', message: '许可证已过期，请续费', expired: true });
      const device = store.devices[device_id];
      if (!device || device.revoked) return sendJson(res, 403, { error: 'DEVICE_REVOKED', message: '此设备授权已被撤销' });
      device.last_seen_at = new Date().toISOString();
      saveStoreSoon();
      return sendJson(res, 200, { valid: true, plan: payload.plan, expires_at: payload.exp > 0 ? new Date(payload.exp * 1000).toISOString() : null });
    }

    // ---- API: macOS refresh ----
    if (method === 'POST' && p === '/api/refresh') {
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const { token, device_id } = body || {};
      const payload = verifyJWT(token, ACTIVATION_SECRET);
      if (!payload || payload.dev !== device_id) return sendJson(res, 401, { error: 'INVALID_TOKEN' });
      if (payload.exp === 0) return sendJson(res, 200, { valid: true, token, refreshed: false });
      const keyRow = store.activation_keys[payload.sub];
      if (!keyRow || keyRow.status === 'revoked') return sendJson(res, 403, { error: 'KEY_REVOKED' });
      if (keyRow.expires_at && new Date(keyRow.expires_at) < new Date()) return sendJson(res, 403, { error: 'SUBSCRIPTION_EXPIRED' });
      const now = Math.floor(Date.now() / 1000);
      let exp = 0;
      if (keyRow.expires_at) exp = Math.min(Math.floor(new Date(keyRow.expires_at).getTime() / 1000), now + TOKEN_LIFETIME);
      const newPayload = { sub: payload.sub, dev: device_id, plan: payload.plan, iat: now, exp };
      const newToken = signJWT(newPayload, ACTIVATION_SECRET);
      const device = store.devices[device_id];
      if (device) { device.license_token = newToken; device.last_seen_at = new Date().toISOString(); }
      saveStoreSoon();
      return sendJson(res, 200, { valid: true, token: newToken, refreshed: true });
    }

    // ---- API: Windows trial (Ed25519) ----
    if (method === 'POST' && p === '/api/trial/windows') {
      const ip = clientIp(req);
      if (!checkRateLimit(ip)) return sendJson(res, 429, { error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' });
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const parsed = parseWindowsTrialBody(body);
      if (!parsed) return sendJson(res, 400, { error: 'INVALID_PARAMS', message: '请求参数无效' });
      const result = await issueWindowsTrial(parsed.device_hash, parsed.device_name, parsed.device_components);
      if (!result.ok) return sendJson(res, result.status, result.body);
      return sendJson(res, 200, {
        license: result.license,
        plan: result.plan,
        expires_at: result.expires_at,
        device_hash: result.device_hash,
      });
    }

    // ---- API: Windows activate (Ed25519) ----
    if (method === 'POST' && p === '/api/activate/windows') {
      const ip = clientIp(req);
      if (!checkRateLimit(ip)) return sendJson(res, 429, { error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' });
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const parsed = parseWindowsBody(body);
      if (!parsed) return sendJson(res, 400, { error: 'INVALID_PARAMS', message: '请求参数无效' });
      if (!WINDOWS_KEY_PATTERN.test(parsed.key.toUpperCase())) return sendJson(res, 400, { error: 'INVALID_KEY', message: '激活码格式不正确' });
      if (parsed.device_hash.length < 16 || parsed.device_hash.length > 128) return sendJson(res, 400, { error: 'INVALID_DEVICE', message: '设备指纹无效' });
      const keyId = parsed.key.toUpperCase();
      const result = await issueWindowsLicense(keyId, parsed.device_hash, parsed.device_name, parsed.device_components);
      if (!result.ok) return sendJson(res, result.status, result.body);
      return sendJson(res, 200, {
        license: result.license,
        plan: result.plan,
        expires_at: result.expires_at,
        device_hash: result.device_hash,
        activation_count: result.activation_count,
        max_activations: result.max_activations,
      });
    }

    // ---- API: offline license download (Ed25519) ----
    if (method === 'POST' && p === '/api/license/download') {
      const ip = clientIp(req);
      if (!checkRateLimit(ip)) return sendJson(res, 429, { error: 'RATE_LIMITED', message: '请求过于频繁，请稍后再试' });
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const parsed = parseWindowsBody(body);
      if (!parsed) return sendJson(res, 400, { error: 'INVALID_PARAMS', message: '请求参数无效' });
      if (!WINDOWS_KEY_PATTERN.test(parsed.key.toUpperCase())) return sendJson(res, 400, { error: 'INVALID_KEY', message: '激活码格式不正确' });
      if (parsed.device_hash.length < 16 || parsed.device_hash.length > 128) return sendJson(res, 400, { error: 'INVALID_DEVICE', message: '设备指纹无效' });
      const keyId = parsed.key.toUpperCase();
      const result = await issueWindowsLicense(keyId, parsed.device_hash, parsed.device_name, parsed.device_components);
      if (!result.ok) return sendJson(res, result.status, result.body);
      const licenseText = JSON.stringify(result.license);
      res.writeHead(200, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Disposition': `attachment; filename="${keyId}.license"`,
        'Access-Control-Allow-Origin': '*',
      });
      return res.end(Buffer.from(licenseText, 'utf8'));
    }

    // ---- API: admin generate windows keys ----
    if (method === 'POST' && p === '/api/admin/generate-windows-keys') {
      if (!requireAdmin(req, res)) return;
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const count = Math.min(parseInt(body.count, 10) || 1, 100);
      const plan = body.plan || 'personal_yearly';

      // Plan-specific defaults
      let maxActivations = DEFAULT_MAX_ACTIVATIONS;
      let durationDays = 0;
      if (plan === 'trial') {
        maxActivations = 1;
        durationDays = 14;
      } else if (plan === 'personal_yearly') {
        maxActivations = 3;
        durationDays = 365;
      } else if (plan === 'personal_lifetime') {
        maxActivations = 3;
        durationDays = 0; // permanent
      }

      // Allow explicit override
      if (body.max_activations !== undefined) maxActivations = parseInt(body.max_activations, 10);
      if (body.duration_days !== undefined) durationDays = parseInt(body.duration_days, 10);

      const keys = [];
      for (let i = 0; i < count; i++) {
        const keyId = generateWindowsKeyId();
        store.windows_keys[keyId] = {
          key_id: keyId,
          plan,
          status: 'available',
          expires_at: null,           // calculated on first activation
          max_activations: maxActivations,
          activation_count: 0,
          duration_days: durationDays,
        };
        keys.push(keyId);
      }
      saveStoreSoon();
      return sendJson(res, 200, { keys, count, plan, max_activations: maxActivations, duration_days: durationDays });
    }

    // ---- API: admin pool stats ----
    if (method === 'GET' && p === '/api/admin/pool-stats') {
      if (!requireAdmin(req, res)) return;
      const stats = { available: 0, activated: 0, exhausted: 0, revoked: 0, total: 0 };
      for (const k of Object.values(store.windows_keys)) {
        const s = k.status === 'unused' ? 'available' : k.status === 'used' ? 'activated' : k.status;
        if (stats[s] !== undefined) stats[s]++;
        stats.total++;
      }
      return sendJson(res, 200, stats);
    }

    // ---- API: admin windows keys list ----
    if (method === 'GET' && p === '/api/admin/windows-keys') {
      if (!requireAdmin(req, res)) return;
      const status = url.searchParams.get('status') || '';
      const plan = url.searchParams.get('plan') || '';
      const search = url.searchParams.get('search') || '';
      const page = Math.max(parseInt(url.searchParams.get('page'), 10) || 1, 1);
      const limit = Math.min(Math.max(parseInt(url.searchParams.get('limit'), 10) || 20, 1), 100);

      let keys = Object.values(store.windows_keys);
      if (status) keys = keys.filter(k => k.status === status);
      if (plan) keys = keys.filter(k => k.plan === plan);
      if (search) {
        const s = search.toUpperCase();
        keys = keys.filter(k =>
          (k.key_id && k.key_id.toUpperCase().includes(s)) ||
          (k.order_id && k.order_id.toUpperCase().includes(s)) ||
          (k.email && k.email.toUpperCase().includes(s))
        );
      }

      const total = keys.length;
      const start = (page - 1) * limit;
      const paginated = keys.slice(start, start + limit);

      return sendJson(res, 200, {
        keys: paginated,
        pagination: { page, limit, total, pages: Math.ceil(total / limit) }
      });
    }

    // ---- API: admin windows key detail ----
    if (method === 'GET' && p.startsWith('/api/admin/windows-keys/') && !p.endsWith('/export')) {
      if (!requireAdmin(req, res)) return;
      const keyId = path.basename(p).toUpperCase();
      const keyRow = store.windows_keys[keyId];
      if (!keyRow) return sendJson(res, 404, { error: 'KEY_NOT_FOUND' });

      const devices = Object.values(store.windows_devices).filter(d => d.key_id === keyId);
      return sendJson(res, 200, { key: keyRow, devices });
    }

    // ---- API: admin export windows keys (plain text for copy) ----
    if (method === 'GET' && p === '/api/admin/windows-keys/export') {
      if (!requireAdmin(req, res)) return;
      const status = url.searchParams.get('status') || 'available';
      const plan = url.searchParams.get('plan') || '';
      let keys = Object.values(store.windows_keys);
      if (status) keys = keys.filter(k => k.status === status);
      if (plan) keys = keys.filter(k => k.plan === plan);
      const text = keys.map(k => k.key_id).join('\n');
      res.writeHead(200, {
        'Content-Type': 'text/plain; charset=utf-8',
        'Access-Control-Allow-Origin': '*',
      });
      return res.end(text);
    }

    // ---- API: admin windows devices list ----
    if (method === 'GET' && p === '/api/admin/windows-devices') {
      if (!requireAdmin(req, res)) return;
      const keyId = (url.searchParams.get('key_id') || '').toUpperCase();
      const search = url.searchParams.get('search') || '';
      const page = Math.max(parseInt(url.searchParams.get('page'), 10) || 1, 1);
      const limit = Math.min(Math.max(parseInt(url.searchParams.get('limit'), 10) || 20, 1), 100);

      let devices = Object.values(store.windows_devices);
      if (keyId) devices = devices.filter(d => d.key_id === keyId);
      if (search) {
        const s = search.toUpperCase();
        devices = devices.filter(d =>
          (d.device_id && d.device_id.toUpperCase().includes(s)) ||
          (d.device_name && d.device_name.toUpperCase().includes(s))
        );
      }

      const total = devices.length;
      const start = (page - 1) * limit;
      const paginated = devices.slice(start, start + limit);

      return sendJson(res, 200, {
        devices: paginated,
        pagination: { page, limit, total, pages: Math.ceil(total / limit) }
      });
    }

    // ---- API: admin revoke Windows device ----
    const revokeDeviceMatch = p.match(/^\/api\/admin\/windows-devices\/([^/]+)\/revoke$/);
    if (method === 'POST' && revokeDeviceMatch) {
      if (!requireAdmin(req, res)) return;

      let deviceId;
      try {
        deviceId = decodeURIComponent(revokeDeviceMatch[1]);
      } catch {
        return sendJson(res, 400, { error: 'INVALID_DEVICE_ID' });
      }

      if (!Object.prototype.hasOwnProperty.call(store.windows_devices, deviceId)) {
        return sendJson(res, 404, { error: 'DEVICE_NOT_FOUND' });
      }
      const device = store.windows_devices[deviceId];
      if (device.revoked) return sendJson(res, 400, { error: 'ALREADY_REVOKED' });

      device.revoked = 1;
      device.revoked_at = new Date().toISOString();

      const keyRow = store.windows_keys[device.key_id];
      if (keyRow && keyRow.activation_count > 0) {
        keyRow.activation_count -= 1;
        if (keyRow.status === 'exhausted') keyRow.status = 'activated';
      }

      saveStoreSoon();
      return sendJson(res, 200, { device_id: deviceId, revoked: true });
    }

    // ---- API: admin generate (macOS) ----
    if (method === 'POST' && p === '/api/admin/generate') {
      if (!requireAdmin(req, res)) return;
      let body;
      try { body = JSON.parse(await readBody(req)); } catch { return sendJson(res, 400, { error: 'INVALID_JSON' }); }
      const count = Math.min(parseInt(body.count, 10) || 1, 100);
      const plan = body.plan || 'personal_lifetime';
      const maxDevices = parseInt(body.max_devices, 10) || 1;
      const keys = [];
      for (let i = 0; i < count; i++) {
        const keyId = generateWindowsKeyId();
        store.activation_keys[keyId] = { key_id: keyId, product: 'xlsone', plan, status: 'unused', max_devices: maxDevices, expires_at: body.expires_at || null };
        keys.push(keyId);
      }
      saveStoreSoon();
      return sendJson(res, 200, { keys, count, plan });
    }

    return sendJson(res, 404, { error: 'NOT_FOUND' });
  } catch (err) {
    console.error('[server] unhandled:', err);
    if (!res.headersSent) sendJson(res, 500, { error: 'INTERNAL_ERROR' });
  }
});

server.listen(PORT, () => {
  console.log(`[xlsone-activation] domestic API listening on :${PORT}`);
  if (!ED25519_PRIVATE_KEY) console.warn('[warn] ED25519_PRIVATE_KEY not set — Windows activation will fail');
  if (!ACTIVATION_SECRET) console.warn('[warn] ACTIVATION_SECRET not set — macOS activation will fail');
  if (!ADMIN_API_KEY) console.warn('[warn] ADMIN_API_KEY not set — admin API disabled');
});
