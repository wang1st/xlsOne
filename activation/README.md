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
npx wrangler secret put ACTIVATION_SECRET     # 设置签名密钥
npx wrangler secret put LEMON_SQUEEZY_SIGNING_SECRET  # LS webhook 签名
npx wrangler deploy
```

## 激活流程

```
用户购买 → Lemon Squeezy webhook → Worker 自动生成激活码 → 邮件发送
           │
用户打开 App → 提示激活 → 输入 XXXX-XXXX-XXXX
           │
           ├─ 在线: POST /api/activate (CF Workers → AliFC fallback)
           │         ↓ 成功 → 存储 Token → 启用全部功能
           │
           └─ 离线: 网页生成 .license 文件 → App 导入 → 本地验签
```

## 国内可达性

| 端点 | 位置 | 目标用户 |
|------|------|---------|
| api.xlsone.com | Cloudflare Workers | 海外 + 国内可通 |
| 阿里云 FC (TBD) | 阿里云函数计算 | 国内兜底 |
| 离线授权文件 | 本地 | 内网/断网用户 |

## 安全

- Token: JWT-like HMAC-SHA256，30天滚动刷新
- 设备绑定: Mac 序列号 + MAC 地址 SHA256 哈希
- 激活码: 16位字母数字分段格式，D1 存储状态机
- 离线授权: HMAC 验签 + 设备码校验

## 待完成

- [ ] 阿里云 FC 国内端点部署
- [ ] Lemon Squeezy webhook 实际对接测试
- [ ] 激活码邮件自动发送（Resend/SendGrid API）
- [ ] 离线授权文件生成网页（z-pulse.cn/offline）
- [ ] macOS 端编译验证
- [ ] 14天免费试用模式
- [ ] 反盗版：代码混淆（后期）
