import { defineStore } from 'pinia'
import { ref } from 'vue'

export const useClipStore = defineStore('clip', () => {
  const lastNotifiedClip = ref(null)

  function notifyClipAdded(data) {
    lastNotifiedClip.value = data
    window.dispatchEvent(new CustomEvent('ws-clip-added', { detail: data }))
  }

  return {
    lastNotifiedClip,
    notifyClipAdded,
  }
})
