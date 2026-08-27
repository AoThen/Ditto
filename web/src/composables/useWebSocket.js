import { ref, onMounted, onUnmounted } from 'vue'
import { useUserStore } from '@/stores/user'
import { useClipStore } from '@/stores/clip'
import { getChanges } from '@/api/clips'
import { ElMessage } from 'element-plus'

const WS_URL = import.meta.env.VITE_WS_URL || ''

// HIGH FIX (H3): Module-level singleton state to ensure only ONE WS connection per tab.
let sharedWs = null
let sharedIsConnected = ref(false)
let sharedReconnectTimer = null
let sharedPingTimer = null
let sharedReconnectAttempts = 0
const MAX_RECONNECT_DELAY = 30000
const MAX_RECONNECT_ATTEMPTS = 20

// FIX: Shared consumer counter so all components coordinate properly
let activeConsumers = 0

function startPingTimer() {
  stopPingTimer()
  sharedPingTimer = setInterval(() => {
    if (sharedWs && sharedWs.readyState === WebSocket.OPEN) {
      sharedWs.send(JSON.stringify({ type: 'ping' }))
    }
  }, 30000) // 30 seconds
}

function stopPingTimer() {
  if (sharedPingTimer) {
    clearInterval(sharedPingTimer)
    sharedPingTimer = null
  }
}

function handleMessage(msg) {
  switch (msg.type) {
    case 'connected':
      console.log('[WS] Server says:', msg.data.message)
      break
    case 'ping':
      break
    case 'clip_added':
      { const clipStore = useClipStore(); if (msg?.data) clipStore.notifyClipAdded(msg.data) }
      break
    case 'clips_added':
      {
        const clips = msg?.data?.clips
        if (Array.isArray(clips)) {
            const clipStore = useClipStore()
            clips.forEach(c => clipStore.notifyClipAdded(c))
            ElMessage.info(`收到 ${clips.length} 条新的剪贴板内容`)
        } else {
            console.warn('[WS] Malformed clips_added message:', msg)
        }
      }
      break
    case 'clips_deleted':
      {
        const clipStore = useClipStore()
        clipStore.notifyClipDeleted(msg.data)
      }
      break
    case 'goaway':
      ElMessage.warning('服务器正在关闭连接')
      disconnect()
      break
    default:
      console.warn('[WS] Unknown message type:', msg.type)
  }
}

// P2 FIX: Fetch incremental changes since last sync to catch up missed data on reconnect
async function fetchSyncCatchUp() {
  try {
    const clipStore = useClipStore()
    const since = clipStore.lastSyncTime || '2000-01-01T00:00:00Z'
    const res = await getChanges(since)
    if (res?.code === 0 && res?.data) {
      const data = res.data
      // Process any new clips from the catch-up
      if (Array.isArray(data.clips) && data.clips.length > 0) {
        data.clips.forEach(clip => clipStore.notifyClipAdded(clip))
      }
      // Process any deletions
      if (Array.isArray(data.deleted_ids) && data.deleted_ids.length > 0) {
        clipStore.notifyClipDeleted({ clip_ids: data.deleted_ids })
      }
      // Update sync time watermark
      if (data.server_time) {
        clipStore.updateSyncTime(data.server_time)
      }
    }
  } catch (e) {
    console.warn('[WS] Catch-up fetch failed:', e)
  }
}

function scheduleReconnect() {  if (sharedReconnectTimer) return

  if (sharedReconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
    console.log(`[WS] Max reconnect attempts (${MAX_RECONNECT_ATTEMPTS}) reached, giving up`)
    ElMessage.error('WebSocket 连接失败次数过多，请刷新页面重试')
    return
  }

  sharedReconnectAttempts++
  const baseDelay = Math.min(1000 * Math.pow(2, sharedReconnectAttempts), MAX_RECONNECT_DELAY)
  const jitter = Math.random() * 1000
  const delay = baseDelay + jitter
  console.log(`[WS] Reconnecting in ${Math.round(delay)}ms (attempt ${sharedReconnectAttempts}/${MAX_RECONNECT_ATTEMPTS})`)

  sharedReconnectTimer = setTimeout(() => {
    sharedReconnectTimer = null
    connect()
  }, delay)
}

function connect() {
  if (sharedWs && sharedWs.readyState === WebSocket.OPEN) return

  // H1: Check auth state via cookie (token is in HttpOnly cookie, not in store)
  const userStore = useUserStore()
  userStore.checkAuthState()
  if (!userStore.isLoggedIn) {
    console.log('[WS] Not logged in, skipping connect')
    return
  }

  // H1: Browser sends HttpOnly cookies automatically during WebSocket handshake.
  // We still pass token via Sec-WebSocket-Protocol as a fallback for edge cases
  // where cookie hasn't propagated yet (e.g., immediately after login).
  const url = WS_URL ? `${WS_URL}/api/v1/ws` : '/api/v1/ws'
  console.log('[WS] Connecting to:', url)

  try {
    sharedWs = new WebSocket(url, ['cookie-auth'])

    sharedWs.onopen = () => {
      console.log('[WS] Connected')
      sharedIsConnected.value = true
      sharedReconnectAttempts = 0
      startPingTimer()
      // P2 FIX: On reconnect, fetch incremental changes to catch up missed data.
      // Only catch up on initial connect or after a reconnection (not on every ping).
      const wasReconnecting = localStorage.getItem('ditto_ws_reconnecting') === '1'
      if (!wasReconnecting) {
        // First connection — just set the flag, no catch-up needed (page load handles it)
        localStorage.setItem('ditto_ws_reconnecting', '1')
      } else {
        // Reconnected — fetch catch-up
        localStorage.removeItem('ditto_ws_reconnecting')
        fetchSyncCatchUp()
      }
    }

    sharedWs.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data)
        handleMessage(msg)
      } catch (e) {
        console.error('[WS] Failed to parse message:', e, event.data)
      }
    }

    sharedWs.onclose = (event) => {
      console.log('[WS] Closed:', event.code, event.reason)
      sharedIsConnected.value = false
      stopPingTimer()
      sharedWs = null
      // Only reconnect if closed abnormally and there are still active consumers
      if ((event.code !== 1000 || event.reason === 'Client disconnect') && activeConsumers > 0) {
        scheduleReconnect()
      }
    }

    sharedWs.onerror = (err) => {
      console.error('[WS] Error:', err)
      ElMessage.error('WebSocket 连接错误，请检查网络')
    }
  } catch (e) {
    console.error('[WS] Failed to create WebSocket:', e)
    scheduleReconnect()
  }
}

function disconnect() {
  if (sharedReconnectTimer) {
    clearTimeout(sharedReconnectTimer)
    sharedReconnectTimer = null
  }
  stopPingTimer()
  if (sharedWs) {
    sharedWs.close(1000, 'Client disconnect')
    sharedWs = null
  }
  sharedIsConnected.value = false
}

function isConnectedToServer() {
  return sharedIsConnected.value && sharedWs && sharedWs.readyState === WebSocket.OPEN
}

/**
 * useWebSocket - Singleton WebSocket composable.
 * All callers in the same tab share the SAME connection AND the SAME consumer counter.
 * Connection persists across route navigation — only closes on explicit disconnect
 * (e.g. logout) or when the last consumer unmounts.
 */
export function useWebSocket() {
  onMounted(() => {
    activeConsumers++
    connect()
  })

  onUnmounted(() => {
    activeConsumers--
    if (activeConsumers <= 0) {
      // Last consumer unmounted — disconnect
      disconnect()
    }
  })

  function disconnectExplicit() {
    activeConsumers = 0
    disconnect()
  }

  return {
    connect,
    disconnect: disconnectExplicit,
    isConnected: sharedIsConnected,
    isConnectedToServer
  }
}

// Also export the singleton disconnect for logout
export { disconnect as disconnectWebSocket }
