'use strict';

const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { setTimeout: delay } = require('node:timers/promises');
const test = require('node:test');


const SERVER_PATH = path.resolve(__dirname, '..', 'server.js');
const ADMIN_API_KEY = 'domestic-server-test-admin-key';


function request(port, pathname, token) {
  return new Promise((resolve, reject) => {
    const headers = {};
    if (token !== undefined) headers.Authorization = `Bearer ${token}`;

    const req = http.request({
      hostname: '127.0.0.1',
      port,
      path: pathname,
      method: 'POST',
      headers,
    }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8');
        let body;
        try {
          body = JSON.parse(text);
        } catch (error) {
          reject(new Error(`invalid JSON response (${res.statusCode}): ${text}`, { cause: error }));
          return;
        }
        resolve({ status: res.statusCode, body });
      });
    });
    req.on('error', reject);
    req.end();
  });
}


function getUnusedPort() {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once('error', reject);
    probe.listen(0, '127.0.0.1', () => {
      const { port } = probe.address();
      probe.close((error) => error ? reject(error) : resolve(port));
    });
  });
}


async function waitForServer(port, child, stderr) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    if (child.exitCode !== null) {
      throw new Error(`server exited with ${child.exitCode}: ${stderr()}`);
    }
    try {
      await new Promise((resolve, reject) => {
        const req = http.get({ hostname: '127.0.0.1', port, path: '/api/health' }, (res) => {
          res.resume();
          res.on('end', resolve);
        });
        req.on('error', reject);
      });
      return;
    } catch {
      await delay(25);
    }
  }
  throw new Error(`server did not start: ${stderr()}`);
}


async function stopServer(child) {
  if (child.exitCode !== null) return;
  child.kill();
  await Promise.race([
    new Promise((resolve) => child.once('exit', resolve)),
    delay(2000).then(() => {
      if (child.exitCode === null) child.kill('SIGKILL');
    }),
  ]);
}


async function withServer(initialStore, callback) {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'xlsone-domestic-server-'));
  const dbPath = path.join(dataDir, 'store.json');
  fs.writeFileSync(dbPath, JSON.stringify(initialStore, null, 2));

  const port = await getUnusedPort();
  let stderr = '';
  const child = spawn(process.execPath, [SERVER_PATH], {
    env: {
      ...process.env,
      PORT: String(port),
      DATA_DIR: dataDir,
      DB_PATH: dbPath,
      DOWNLOADS_DIR: path.join(dataDir, 'downloads'),
      ADMIN_API_KEY,
      ACTIVATION_SECRET: 'test-activation-secret',
      ED25519_PRIVATE_KEY: '',
    },
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (chunk) => { stderr += chunk; });

  try {
    await waitForServer(port, child, () => stderr);
    await callback({
      port,
      readStore: () => JSON.parse(fs.readFileSync(dbPath, 'utf8')),
      waitForStore: async (predicate) => {
        const deadline = Date.now() + 3000;
        while (Date.now() < deadline) {
          try {
            const persisted = JSON.parse(fs.readFileSync(dbPath, 'utf8'));
            if (predicate(persisted)) return persisted;
          } catch {}
          await delay(25);
        }
        throw new Error(`store was not persisted in time: ${stderr}`);
      },
    });
  } finally {
    await stopServer(child);
    fs.rmSync(dataDir, { recursive: true, force: true });
  }
}


function makeStore({ revoked = 0, activationCount = 3, status = 'exhausted' } = {}) {
  return {
    windows_keys: {
      'AAAA-BBBB-CCCC-DDDD': {
        key_id: 'AAAA-BBBB-CCCC-DDDD',
        plan: 'personal_yearly',
        status,
        max_activations: 3,
        activation_count: activationCount,
      },
    },
    windows_devices: {
      'device-123': {
        device_id: 'device-123',
        key_id: 'AAAA-BBBB-CCCC-DDDD',
        device_name: 'Test device',
        activated_at: '2026-07-20T00:00:00.000Z',
        last_seen_at: '2026-07-20T00:00:00.000Z',
        revoked,
        ...(revoked ? { revoked_at: '2026-07-20T01:00:00.000Z' } : {}),
      },
    },
  };
}


test('device revoke requires administrator authentication', async () => {
  const initialStore = makeStore();
  await withServer(initialStore, async ({ port, readStore }) => {
    const response = await request(
      port,
      '/api/admin/windows-devices/device-123/revoke',
    );

    assert.equal(response.status, 401);
    assert.deepEqual(response.body, { error: 'UNAUTHORIZED' });
    assert.equal(readStore().windows_devices['device-123'].revoked, 0);
  });
});


test('device revoke returns 404 for an unknown device', async () => {
  await withServer(makeStore(), async ({ port, readStore }) => {
    const response = await request(
      port,
      '/api/admin/windows-devices/missing-device/revoke',
      ADMIN_API_KEY,
    );

    assert.equal(response.status, 404);
    assert.deepEqual(response.body, { error: 'DEVICE_NOT_FOUND' });
    assert.equal(readStore().windows_keys['AAAA-BBBB-CCCC-DDDD'].activation_count, 3);
  });
});


test('device revoke cannot address inherited object properties', async () => {
  await withServer(makeStore(), async ({ port }) => {
    const response = await request(
      port,
      '/api/admin/windows-devices/__proto__/revoke',
      ADMIN_API_KEY,
    );

    assert.equal(response.status, 404);
    assert.deepEqual(response.body, { error: 'DEVICE_NOT_FOUND' });
    assert.equal(Object.prototype.revoked, undefined);
  });
});


test('device revoke rejects an already revoked device without changing the key', async () => {
  await withServer(makeStore({ revoked: 1 }), async ({ port, readStore }) => {
    const response = await request(
      port,
      '/api/admin/windows-devices/device-123/revoke',
      ADMIN_API_KEY,
    );

    assert.equal(response.status, 400);
    assert.deepEqual(response.body, { error: 'ALREADY_REVOKED' });
    const persisted = readStore();
    assert.equal(persisted.windows_devices['device-123'].revoked_at, '2026-07-20T01:00:00.000Z');
    assert.equal(persisted.windows_keys['AAAA-BBBB-CCCC-DDDD'].activation_count, 3);
    assert.equal(persisted.windows_keys['AAAA-BBBB-CCCC-DDDD'].status, 'exhausted');
  });
});


test('device revoke persists the device and frees an exhausted activation slot', async () => {
  await withServer(makeStore(), async ({ port, waitForStore }) => {
    const before = Date.now();
    const response = await request(
      port,
      '/api/admin/windows-devices/device-123/revoke',
      ADMIN_API_KEY,
    );

    assert.equal(response.status, 200);
    assert.deepEqual(response.body, { device_id: 'device-123', revoked: true });

    const persisted = await waitForStore(
      (current) => current.windows_devices['device-123'].revoked === 1,
    );
    const device = persisted.windows_devices['device-123'];
    const key = persisted.windows_keys['AAAA-BBBB-CCCC-DDDD'];
    assert.equal(device.revoked, 1);
    assert.ok(Number.isFinite(Date.parse(device.revoked_at)));
    assert.ok(Date.parse(device.revoked_at) >= before);
    assert.equal(key.activation_count, 2);
    assert.equal(key.status, 'activated');
  });
});
