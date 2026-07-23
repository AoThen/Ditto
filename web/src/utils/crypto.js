const DOMAIN_TAG = 'DITTO_ENC_AUTH_v1'

function strToBuf(s) {
  return new TextEncoder().encode(s)
}

export function bufToBase64(buf) {
  const bytes = new Uint8Array(buf)
  let binary = ''
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i])
  }
  return btoa(binary)
}

function base64ToBuf(b64) {
  return Uint8Array.from(atob(b64), c => c.charCodeAt(0))
}

export async function deriveKEK(password, saltB64) {
  const salt = base64ToBuf(saltB64)
  const keyMaterial = await crypto.subtle.importKey(
    'raw', strToBuf(password), 'PBKDF2', false, ['deriveKey']
  )
  return crypto.subtle.deriveKey(
    { name: 'PBKDF2', salt, iterations: 100000, hash: 'SHA-256' },
    keyMaterial,
    { name: 'AES-GCM', length: 256 },
    false, ['wrapKey', 'unwrapKey']
  )
}

export async function generateDEK() {
  return crypto.subtle.generateKey(
    { name: 'AES-GCM', length: 256 },
    true, ['encrypt', 'decrypt']
  )
}

export async function wrapDEK(KEK, DEK) {
  const iv = crypto.getRandomValues(new Uint8Array(12))
  const wrapped = await crypto.subtle.wrapKey('raw', DEK, KEK, { name: 'AES-GCM', iv })
  const wrappedBytes = new Uint8Array(wrapped)
  const combined = new Uint8Array(iv.length + wrappedBytes.length)
  combined.set(iv)
  combined.set(wrappedBytes, iv.length)
  return bufToBase64(combined)
}

export async function unwrapDEK(KEK, wrappedB64) {
  const combined = base64ToBuf(wrappedB64)
  const iv = combined.slice(0, 12)
  const wrapped = combined.slice(12)
  return crypto.subtle.unwrapKey(
    'raw', wrapped, KEK, { name: 'AES-GCM', iv },
    { name: 'AES-GCM', length: 256 },
    true, ['encrypt', 'decrypt']
  )
}

export async function computeVerificationHash(password, saltB64) {
  const data = DOMAIN_TAG + ':' + password + ':' + saltB64
  const hash = await crypto.subtle.digest('SHA-256', strToBuf(data))
  return bufToBase64(hash)
}