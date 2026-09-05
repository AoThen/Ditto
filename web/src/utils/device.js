const INSTALL_ID_KEY = 'ditto_install_id'

// Stable per-browser identifier sent at login. Without it the server derives the
// device row from the device name, which every browser reports as "unknown", so
// all of one user's browsers would share a single device (and token version).
export function getInstallId() {
  let id = localStorage.getItem(INSTALL_ID_KEY)
  if (!id) {
    id = window.crypto?.randomUUID
      ? window.crypto.randomUUID()
      : Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10)
    localStorage.setItem(INSTALL_ID_KEY, id)
  }
  return id
}
