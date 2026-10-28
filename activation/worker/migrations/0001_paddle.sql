-- Apply after schema.sql. This migration does not change domestic license tables.
CREATE TABLE IF NOT EXISTS paddle_checkout_sessions (
  session_id TEXT PRIMARY KEY,
  claim_hash TEXT NOT NULL UNIQUE,
  price_id TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  purchase_expires_at INTEGER NOT NULL,
  claim_expires_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS paddle_subscriptions (
  subscription_id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL UNIQUE,
  key_id TEXT NOT NULL UNIQUE,
  customer_id TEXT NOT NULL,
  price_id TEXT NOT NULL,
  status TEXT NOT NULL,
  current_period_end TEXT,
  paid_until TEXT,
  last_snapshot_at INTEGER NOT NULL DEFAULT 0,
  last_snapshot_event TEXT NOT NULL DEFAULT '',
  last_payment_at INTEGER NOT NULL DEFAULT 0,
  last_payment_event TEXT NOT NULL DEFAULT '',
  last_transaction_id TEXT
);

CREATE TABLE IF NOT EXISTS paddle_events (
  event_id TEXT PRIMARY KEY,
  event_type TEXT NOT NULL,
  occurred_at INTEGER NOT NULL,
  received_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS paddle_rate_limits (
  bucket TEXT PRIMARY KEY,
  requests INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_paddle_sessions_expiry ON paddle_checkout_sessions(claim_expires_at);
CREATE INDEX IF NOT EXISTS idx_paddle_limits_expiry ON paddle_rate_limits(expires_at);
