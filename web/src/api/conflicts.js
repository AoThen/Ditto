import request from '@/api/request'

// List all conflict clips
export function listConflictClips(page = 1, perPage = 20) {
  return request({
    url: '/api/v1/clips/conflicts',
    method: 'get',
    params: { page, per_page: perPage }
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
