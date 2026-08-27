import { defineStore } from 'pinia'
import { ref } from 'vue'

const SYNC_TIME_KEY = 'ditto_last_sync_time'

export const useClipStore = defineStore('clip', () => {
  const lastNotifiedClip = ref(null)
  // Persist last sync time to localStorage so reconnect can catch up
  const lastSyncTime = ref(localStorage.getItem(SYNC_TIME_KEY) || null)

  function notifyClipAdded(data) {
    lastNotifiedClip.value = data
    window.dispatchEvent(new CustomEvent('ws-clip-added', { detail: data }))
  }

  function notifyClipDeleted(data) {
    const clipIds = data?.clip_ids || []
    window.dispatchEvent(new CustomEvent('ws-clips-deleted', { detail: clipIds }))
  }

  function updateSyncTime(timestamp) {
    lastSyncTime.value = timestamp
    localStorage.setItem(SYNC_TIME_KEY, timestamp)
  }

  return {
    lastNotifiedClip,
    lastSyncTime,
    notifyClipAdded,
    notifyClipDeleted,
    updateSyncTime,
  }
})
