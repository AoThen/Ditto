import { ref, onUnmounted } from 'vue'
import { useUserStore } from '@/stores/user'
import { ElMessage } from 'element-plus'

const WS_URL = import.meta.env.VITE_WS_URL || 'ws://localhost:8080'

export function useWebSocket() {
  const ws = ref(null)
  const isConnected = ref(false)
  const userStore = useUserStore()

  let reconnectTimer = null
  let pingTimer = null
  const MAX_RECONNECT_DELAY = 30000 // 30 seconds
  let reconnectAttempts = 0

  function connect() {
    if (ws.value) return

    const token = userStore.token
    if (!token) {
      console.log('[WS] No token, skipping connect')
      return
    }

    const url = `${WS_URL}/api/v1/ws?token=${encodeURIComponent(token)}`
    console.log('[WS] Connecting to:', url.replace(token, '***'))

    try {
      ws.value = new WebSocket(url)

      ws.value.onopen = () => {
        console.log('[WS] Connected')
        isConnected.value = true
        reconnectAttempts = 0
        startPingTimer()
      }

      ws.value.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data)
          handleMessage(msg)
        } catch (e) {
          console.error('[WS] Failed to parse message:', e, event.data)
        }
      }

      ws.value.onclose = (event) => {
        console.log('[WS] Closed:', event.code, event.reason)
        isConnected.value = false
        stopPingTimer()
        ws.value = null
        scheduleReconnect()
      }

      ws.value.onerror = (err) => {
        console.error('[WS] Error:', err)
        ElMessage.error('WebSocket 连接错误')
      }
    } catch (e) {
      console.error('[WS] Failed to create WebSocket:', e)
      scheduleReconnect()
    }
  }

  function disconnect() {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
    stopPingTimer()
    if (ws.value) {
      ws.value.close(1000, 'Client disconnect')
      ws.value = null
    }
    isConnected.value = false
  }

  function handleMessage(msg) {
    switch (msg.type) {
      case 'connected':
        console.log('[WS] Server says:', msg.data.message)
        break
      case 'ping':
        // Server ping, no action needed (pong handled by browser)
        break
      case 'clip_added':
        // Notify listeners via custom event
        window.dispatchEvent(new CustomEvent('ws-clip-added', { detail: msg.data }))
        ElMessage.info('收到新的剪贴板内容')
        break
      case 'goaway':
        ElMessage.warning('服务器正在关闭连接')
        disconnect()
        break
      default:
        console.log('[WS] Unknown message type:', msg.type)
    }
  }

  function startPingTimer() {
    stopPingTimer()
    pingTimer = setInterval(() => {
      if (ws.value && ws.value.readyState === WebSocket.OPEN) {
        ws.value.send(JSON.stringify({ type: 'ping' }))
      }
    }, 30000) // 30 seconds
  }

  function stopPingTimer() {
    if (pingTimer) {
      clearInterval(pingTimer)
      pingTimer = null
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer) return

    reconnectAttempts++
    const delay = Math.min(1000 * Math.pow(2, reconnectAttempts), MAX_RECONNECT_DELAY)
    console.log(`[WS] Reconnecting in ${delay}ms (attempt ${reconnectAttempts})`)

    reconnectTimer = setTimeout(() => {
      reconnectTimer = null
      connect()
    }, delay)
  }

  function isConnectedToServer() {
    return isConnected.value && ws.value && ws.value.readyState === WebSocket.OPEN
  }

  return {
    connect,
    disconnect,
    isConnected,
    isConnectedToServer
  }
}
