import request from '@/api/request'

// List all conflict clips
export function listConflictClips() {
  return request({
    url: '/api/v1/clips/conflicts',
    method: 'get'
  })
}

// Resolve a conflict clip (accept or discard)
export function resolveConflictClip(id, action) {
  return request({
    url: `/api/v1/clips/conflicts/${id}/resolve`,
    method: 'post',
    data: { action }
  })
}
