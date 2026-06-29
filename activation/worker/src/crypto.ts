// Minimal crypto helpers for Cloudflare Workers
// Uses Web Crypto API (available in Workers runtime)

export async function createHmac(algorithm: string, key: string): Promise<{
  update(data: string): void
  digest(format: 'hex'): string
}> {
  const encoder = new TextEncoder()
  const cryptoKey = await crypto.subtle.importKey(
    'raw',
    encoder.encode(key),
    { name: 'HMAC', hash: algorithm.replace('sha', 'SHA-') },
    false,
    ['sign']
  )

  let data = ''

  return {
    update(d: string) {
      data += d
    },
    digest(_format: 'hex'): string {
      // Return placeholder — actual signing uses Web Crypto directly in index.ts
      // This module exists for the Hono import to resolve
      return ''
    },
  }
}

export function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false
  let result = 0
  for (let i = 0; i < a.length; i++) {
    result |= a.charCodeAt(i) ^ b.charCodeAt(i)
  }
  return result === 0
}
