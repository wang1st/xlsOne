window.XLSONE_CONFIG = Object.freeze({
  apiBase: 'https://api.xlsone.com',
  customerPortal: 'https://customer-portal.paddle.com/cpl_01jf9na30n6c0s1esnsk3f6cpb',
  billing: {
    enabled: true,
    clientToken: 'live_660e28a086b4d110aaa558da9bc',
    monthly: { priceId: 'pri_01m62za09xh2ky5chjggyy9h2n', displayPrice: 'US$4.99' },
    yearly: { priceId: 'pri_01m62zc4eksrh6b09h6d4z2px7', displayPrice: 'US$39.99' }
  },
  // Public international-v1.1.5 release; checksums match the verified CI artifacts.
  downloads: [
    {
      "platform": "windows",
      "label": "Windows installer",
      "version": "1.1.5",
      "requirements": "Windows x64 · Unsigned installer",
      "format": "MSI",
      "url": "https://github.com/wang1st/xlsOne/releases/download/international-v1.1.5/xlsone-1.1.5-windows-amd64.msi",
      "sha256": "098b84542a2cf88f7d244084b5c5ec63f6259dbe66121f273de21ceff1464c92",
      "bytes": 1470464,
      "verified": true
    },
    {
      "platform": "windows",
      "label": "Windows portable",
      "version": "1.1.5",
      "requirements": "Windows x64 · Unsigned application",
      "format": "ZIP",
      "url": "https://github.com/wang1st/xlsOne/releases/download/international-v1.1.5/xlsone-1.1.5-windows-amd64.zip",
      "sha256": "68126a5be724821f39167b35dcb08970ff8332603d9b181f47885ae1c094b806",
      "bytes": 1127475,
      "verified": true
    },
    {
      "platform": "macos",
      "label": "macOS universal",
      "version": "1.1.5",
      "requirements": "macOS 12+ · Apple Silicon and Intel · Not Apple-notarized",
      "format": "DMG",
      "url": "https://github.com/wang1st/xlsOne/releases/download/international-v1.1.5/xlsOne-1.1.5-macos-universal.dmg",
      "sha256": "ae3ac929d2664839bcd70a98488bfa01bf70661dc4fc13f56808811e248407d6",
      "bytes": 2155492,
      "verified": true
    },
    {
      "platform": "linux",
      "label": "Linux x86_64 (amd64)",
      "version": "1.1.5",
      "requirements": "Debian-based Linux · x86_64 (amd64)",
      "format": "DEB",
      "url": "https://github.com/wang1st/xlsOne/releases/download/international-v1.1.5/xlsOne-1.1.5-linux-amd64.deb",
      "sha256": "e6a6bc18517e032da5676fecac49e524f6f4f9095ac990680720603c4b2d9b53",
      "bytes": 1229834,
      "verified": true
    },
    {
      "platform": "linux",
      "label": "Linux ARM64",
      "version": "1.1.5",
      "requirements": "Debian-based Linux · ARM64 (aarch64)",
      "format": "DEB",
      "url": "https://github.com/wang1st/xlsOne/releases/download/international-v1.1.5/xlsOne-1.1.5-linux-arm64.deb",
      "sha256": "c64355921798f06e35dfd2256f9d825b6d62e0b92728892abc11d4d783da6d08",
      "bytes": 1183660,
      "verified": true
    }
  ]
});
