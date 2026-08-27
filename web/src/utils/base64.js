// Decode base64 bytes into a UTF-8 string.
// Backend stores clipboard format data as base64(UTF-8 bytes).
export function base64ToUtf8(b64) {
  if (!b64) return null
  try {
    const binary = atob(b64)
    const bytes = new Uint8Array(binary.length)
    for (let i = 0; i < binary.length; i++) {
      bytes[i] = binary.charCodeAt(i)
    }
    return new TextDecoder('utf-8').decode(bytes)
  } catch (e) {
    return null
  }
}