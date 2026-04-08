import request from '@/api/request'

// List all groups
export function listGroups() {
  return request({
    url: '/api/v1/groups',
    method: 'get'
  })
}

// Get group detail with children
export function getGroup(id) {
  return request({
    url: `/api/v1/groups/${id}`,
    method: 'get'
  })
}

// Create group
export function createGroup(data) {
  return request({
    url: '/api/v1/groups',
    method: 'post',
    data
  })
}

// Update group
export function updateGroup(id, data) {
  return request({
    url: `/api/v1/groups/${id}`,
    method: 'put',
    data
  })
}

// Delete group
export function deleteGroup(id) {
  return request({
    url: `/api/v1/groups/${id}`,
    method: 'delete'
  })
}

// Move clips to group
export function moveClipsToGroup(groupId, clipIds) {
  return request({
    url: `/api/v1/groups/${groupId}/move-clips`,
    method: 'post',
    data: { clip_ids: clipIds }
  })
}

// Remove clips from group
export function removeClipsFromGroup(clipIds) {
  return request({
    url: '/api/v1/clips/remove-from-group',
    method: 'post',
    data: { clip_ids: clipIds }
  })
}
