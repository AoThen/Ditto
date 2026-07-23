import request from './request'

export function getUsers(params) {
  return request.get('/api/v1/admin/users', { params })
}

export function getUser(id) {
  return request.get(`/api/v1/admin/users/${id}`)
}

export function createUser(data) {
  return request.post('/api/v1/admin/users', data)
}

export function updateUser(id, data) {
  return request.put(`/api/v1/admin/users/${id}`, data)
}

export function deleteUser(id) {
  return request.delete(`/api/v1/admin/users/${id}`)
}

export function resetPassword(id, data) {
  return request.post(`/api/v1/admin/users/${id}/reset-password`, data)
}