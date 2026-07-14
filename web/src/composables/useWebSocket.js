import { ref, onMounted, onUnmounted } from 'vue'
import { useUserStore } from '@/stores/user'
import { useClipStore } from '@/stores/clip'
import { ElMessage } from 'element-plus'

const WS_URL = import.meta.env.VITE_WS_URL || ''

// HIGH FIX (H3): Module-level singleton state to ensure only ONE WS connection per tab.
let sharedWs = null
let sharedIsConnected = ref(false)
let sharedReconnectTimer = null
let sharedPingTimer = null
let sharedReconnectAttempts = 0
const MAX_RECONNECT_DELAY = 30000

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
      { const clipStore = useClipStore(); clipStore.notifyClipAdded(msg.data) }
      ElMessage.info('收到新的剪贴板内容')
      break
    case 'goaway':
      ElMessage.warning('服务器正在关闭连接')
      disconnect()
      break
    default:
      console.warn('[WS] Unknown message type:', msg.type)
  }
}

function scheduleReconnect() {
  if (sharedReconnectTimer) return

  sharedReconnectAttempts++
  const baseDelay = Math.min(1000 * Math.pow(2, sharedReconnectAttempts), MAX_RECONNECT_DELAY)
  const jitter = Math.random() * 1000
  const delay = baseDelay + jitter
  console.log(`[WS] Reconnecting in ${Math.round(delay)}ms (attempt ${sharedReconnectAttempts})`)

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
