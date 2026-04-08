import request from '@/api/request'

// Get statistics overview
export function getStatsOverview() {
  return request({
    url: '/api/v1/stats/overview',
    method: 'get'
  })
}

// Get sync logs
export function getSyncLogs(params = {}) {
  return request({
    url: '/api/v1/stats/sync-logs',
    method: 'get',
    params
  })
}
