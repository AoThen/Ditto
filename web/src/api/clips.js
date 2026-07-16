import request from '@/api/request'

export function listClips(params = {}) {
  return request({
    url: '/api/v1/clips',
    method: 'get',
    params
  })
}

export function getClip(id) {
  return request({
    url: `/api/v1/clips/${id}`,
    method: 'get'
  })
}

export function getChanges(since) {
  return request({
    url: '/api/v1/clips/changes',
    method: 'get',
    params: { since }
  })
}

export function deleteClip(id) {
  return request({
    url: `/api/v1/clips/${id}`,
    method: 'delete'
  })
}

export function getEncryptionSalt() {
  return request({
    url: '/api/v1/encryption/salt',
    method: 'get'
  })
}

export function getKeyMaterial() {
  return request({
    url: '/api/v1/encryption/key-material',
    method: 'get'
  })
}

export function setupEncryption(data) {
  return request({
    url: '/api/v1/encryption/setup',
    method: 'post',
    data
  })
}

export function disableEncryption() {
  return request({
    url: '/api/v1/encryption/disable',
    method: 'post'
  })
}

export function changeEncryptionPassword(data) {
  return request({
    url: '/api/v1/encryption/change-password',
    method: 'post',
    data
  })
}

export function batchDeleteClips(ids) {
  return request({
    url: '/api/v1/clips/batch-delete',
    method: 'post',
    data: { ids }
  })
}

export function batchMarkDontSync(ids) {
  return request({
    url: '/api/v1/clips/batch-dont-sync',
    method: 'post',
    data: { ids }
  })
}