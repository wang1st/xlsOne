# xlsOne 激活系统

## 架构

```
activation/
├── worker/                 # Cloudflare Worker（激活 API）
│   ├── src/
│   │   ├── index.ts        # 主 API：activate / verify / refresh / webhook / admin
│   │   └── crypto.ts       # Web Crypto 辅助
│   ├── schema.sql          # D1 数据库表结构
│   ├── wrangler.toml       # Cloudflare 部署配置
│   ├── package.json
│   └── tsconfig.json
│
└── mac/                    # macOS 客户端模块
    └── (见 Sources/xlsOneLicense/)
```

```
Sources/xlsOneLicense/
├── LicenseManager.swift     # 核心：激活/验证/刷新/离线
├── LicenseActivationView.swift  # 激活界面 + 状态指示器
├── Keychain.swift           # 安全存储 Token
└── DeviceFingerprint.swift  # 设备指纹（序列号/MAC 哈希）
```

## 部署 Worker

```bash
cd activation/worker
npm install
npx wrangler d1 create xlsone-license
npx wrangler d1 execute xlsone-license --file=./schema.sql
npx wrangler secret put ACTIVATION_SECRET     # 旧版 JWT 签名密钥
npx wrangler secret put LEMON_SQUEEZY_SIGNING_SECRET  # LS webhook 签名

# Windows 版 Ed25519 授权所需
npx wrangler secret put ED25519_PRIVATE_KEY   # 64 字符十六进制种子
npx wrangler secret put AFDIAN_USER_ID        # 爱发电用户 ID
npx wrangler secret put AFDIAN_TOKEN          # 爱发电开放平台 Token
npx wrangler secret put AFDIAN_WEBHOOK_TOKEN  # 爱发电 Webhook Token

npx wrangler deploy
```

### Ed25519 密钥对

Windows 客户端内置公钥，Worker 使用对应私钥签名授权文件。当前示例密钥对：

- 种子（私钥，仅保存在 Worker Secret，用 `wrangler secret put ED25519_PRIVATE_KEY` 设置）
- 公钥（嵌入 `cpp/core/src/license_manager.cpp` 的 `kLicensePublicKey` 数组）

> ⚠️ 示例密钥对已从文档/配置中移除，禁止复用——它曾暴露在仓库里，任何人都能用它伪造 Windows 授权。生产环境请务必重新生成密钥对。

```bash
openssl genpkey -algorithm ed25519 -outform DER -out ed25519_priv.der
SEED=$(dd if=ed25519_priv.der bs=1 skip=16 count=32 2>/dev/null | xxd -p | tr -d '\n')
PUB=$(openssl pkey -in ed25519_priv.der -inform DER -pubout -outform DER -out /dev/stdout 2>/dev/null | dd bs=1 skip=12 count=32 2>/dev/null | xxd -p | tr -d '\n')
echo "ED25519_PRIVATE_KEY=$SEED"
echo "PUBLIC_KEY=$PUB"
# 将 $SEED 写入 Wrangler Secret，将 $PUB 更新到 cpp/core/src/license_manager.cpp
```

## 激活流程

### 旧版（HMAC/JWT）

```
用户购买 → Lemon Squeezy webhook → Worker 自动生成激活码 → 邮件发送
           │
用户打开 App → 提示激活 → 输入 XXXX-XXXX-XXXX
           │
           ├─ 在线: POST /api/activate
           │         ↓ 成功 → 存储 Token → 启用全部功能
           │
           └─ 离线: 网页生成 .license 文件 → App 导入 → 本地验签
```

### Windows 版（Ed25519 签名授权文件）

```
用户购买 → 爱发电支付 → Webhook /api/webhook/afdian
           │
           ├─ Worker 生成 XLS1-XXXX-XXXX 激活码
           │
           └─ 爱发电私信自动发送激活码
           │
用户打开 Windows 客户端 → 输入激活码
           │
           ├─ 在线: POST /api/activate/windows
           │         ↓ 成功 → 返回带 Ed25519 签名的 license JSON → 本地保存 → 启用全部功能
           │
           └─ 离线: 复制本机设备码 → 网页申请授权文件 → 导入 .license → 本地 Ed25519 验签
```

## API 端点

| 端点 | 说明 |
|------|------|
| `POST /api/activate` | 旧版激活（HMAC/JWT） |
| `POST /api/verify` | 验证旧版 Token |
| `POST /api/refresh` | 刷新旧版 Token |
| `POST /api/webhook/lemonsqueezy` | Lemon Squeezy 订单回调 |
| `POST /api/admin/generate` | 旧版激活码批量生成 |
| `POST /api/activate/windows` | Windows 版 Ed25519 许可证激活 |
| `POST /api/license/download` | 离线激活：下载签名授权文件 |
| `POST /api/webhook/afdian` | 爱发电订单回调（Windows 版） |
| `POST /api/admin/generate-windows-keys` | Windows 激活码批量生成 |
| `GET /api/health` | 健康检查 |

## 国内可达性

| 端点 | 位置 | 目标用户 |
|------|------|---------|
| api.xlsone.com | Cloudflare Workers | 海外 + 国内可通 |
| 阿里云 FC (TBD) | 阿里云函数计算 | 国内兜底 |
| 离线授权文件 | 本地 | 内网/断网用户 |

## 安全

- 旧版 Token: JWT-like HMAC-SHA256，30天滚动刷新
- Windows 授权: Ed25519 数字签名，客户端内置公钥本地验签
- 设备绑定: Windows 采集主板/CPU/硬盘序列号 + MachineGuid 组合哈希
- 设备迁移: 每年最多 3 次换机，由 `/api/activate/windows` 控制
- 激活码: `XLS1-XXXX-XXXX` 分段格式，D1 存储状态机
- 离线授权: Ed25519 验签 + 设备码校验，支持 2/3 组件部分匹配

## 待完成

- [ ] 阿里云 FC 国内端点部署
- [ ] Lemon Squeezy webhook 实际对接测试
- [ ] 爱发电 webhook 实际对接测试
- [ ] 激活码邮件自动发送（Resend/SendGrid API）
- [ ] 离线授权文件生成网页（z-pulse.cn/offline）
- [ ] macOS 端编译验证
- [ ] 反盗版：代码混淆（后期）
