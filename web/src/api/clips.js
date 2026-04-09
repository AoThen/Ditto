import request from '@/api/request'

// List clips with pagination
export function listClips(params = {}) {
  return request({
    url: '/api/v1/clips',
    method: 'get',
    params
  })
}

// Get single clip detail
export function getClip(id) {
  return request({
    url: `/api/v1/clips/${id}`,
    method: 'get'
  })
}

// Delete clip
export function deleteClip(id) {
  return request({
    url: `/api/v1/clips/${id}`,
    method: 'delete'
  })
}

// Get encryption salt
export function getEncryptionSalt() {
  return request({
    url: '/api/v1/encryption/salt',
    method: 'get'
  })
}

// Setup encryption
export function setupEncryption(data) {
  return request({
    url: '/api/v1/encryption/setup',
    method: 'post',
    data
  })
}

// Disable encryption
export function disableEncryption() {
  return request({
    url: '/api/v1/encryption/disable',
    method: 'post'
  })
}

// Change encryption password (updates hint only)
export function changeEncryptionPassword(passwordHint) {
  return request({
    url: '/api/v1/encryption/change-password',
    method: 'post',
    data: { password_hint: passwordHint }
  })
}
