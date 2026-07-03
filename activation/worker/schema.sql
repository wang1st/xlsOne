-- 激活密钥表（预生成或 Lemon Squeezy webhook 创建）
CREATE TABLE IF NOT EXISTS activation_keys (
  key_id      TEXT PRIMARY KEY,          -- XLS1-A2B3-C4D5
  product     TEXT NOT NULL DEFAULT 'xlsone',  -- 产品标识
  plan        TEXT NOT NULL,             -- personal_yearly / personal_lifetime / enterprise_10
  status      TEXT NOT NULL DEFAULT 'unused',  -- unused / used / revoked
  max_devices INTEGER NOT NULL DEFAULT 1,
  expires_at  TEXT,                      -- 订阅到期日 (ISO8601)，永久版为 NULL
  created_at  TEXT NOT NULL DEFAULT (datetime('now')),
  used_at     TEXT,
  order_id    TEXT,                      -- Lemon Squeezy order ID
  email       TEXT                       -- 购买者邮箱
);

-- 设备注册表
CREATE TABLE IF NOT EXISTS devices (
  device_id   TEXT PRIMARY KEY,          -- SHA256(mac_address + machine_serial)
  key_id      TEXT NOT NULL REFERENCES activation_keys(key_id),
  license_token TEXT NOT NULL,           -- 签发的 JWT-like token
  device_name TEXT,                      -- 用户可读的设备名
  activated_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  revoked     INTEGER NOT NULL DEFAULT 0
);

-- 设备迁移记录
CREATE TABLE IF NOT EXISTS device_migrations (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  key_id      TEXT NOT NULL,
  old_device  TEXT NOT NULL,
  new_device  TEXT NOT NULL,
  migrated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Windows 付费版激活密钥表（爱发电 / 手动生成）
CREATE TABLE IF NOT EXISTS windows_keys (
  key_id      TEXT PRIMARY KEY,          -- XLS1-A2B3-C4D5
  plan        TEXT NOT NULL DEFAULT 'personal_lifetime',
  status      TEXT NOT NULL DEFAULT 'unused',  -- unused / used / revoked
  order_id    TEXT,                      -- 爱发电订单号
  email       TEXT,                      -- 购买者邮箱
  device_hash TEXT,                      -- 绑定设备指纹
  device_components TEXT,                -- JSON 数组，用于本地部分匹配
  activated_at TEXT,
  created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Windows 版设备注册表
CREATE TABLE IF NOT EXISTS windows_devices (
  device_id   TEXT NOT NULL,
  key_id      TEXT NOT NULL REFERENCES windows_keys(key_id),
  device_name TEXT,
  activated_at TEXT NOT NULL DEFAULT (datetime('now')),
  last_seen_at TEXT NOT NULL DEFAULT (datetime('now')),
  revoked     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (device_id, key_id)
);

-- Windows 版设备迁移记录
CREATE TABLE IF NOT EXISTS windows_device_migrations (
  id          INTEGER PRIMARY KEY AUTOINCREMENT,
  key_id      TEXT NOT NULL,
  old_device  TEXT NOT NULL,
  new_device  TEXT NOT NULL,
  migrated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- 索引
CREATE INDEX IF NOT EXISTS idx_keys_status ON activation_keys(status);
CREATE INDEX IF NOT EXISTS idx_devices_key ON devices(key_id);
CREATE INDEX IF NOT EXISTS idx_keys_email ON activation_keys(email);
CREATE INDEX IF NOT EXISTS idx_windows_keys_status ON windows_keys(status);
CREATE INDEX IF NOT EXISTS idx_windows_keys_order ON windows_keys(order_id);
CREATE INDEX IF NOT EXISTS idx_windows_devices_key ON windows_devices(key_id);
