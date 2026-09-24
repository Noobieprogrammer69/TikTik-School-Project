class RealtimeClient {
  socket = null
  listeners = new Set()
  reconnectTimer = null
  stopped = true

  start() {
    this.stopped = false
    if (this.socket?.readyState === window.WebSocket.OPEN || this.socket?.readyState === window.WebSocket.CONNECTING) return
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    this.socket = new window.WebSocket(`${protocol}//${window.location.host}/api/realtime`)
    this.socket.addEventListener('message', (event) => {
      try {
        const value = JSON.parse(event.data)
        this.listeners.forEach((listener) => listener(value))
      } catch {
        // Ignore malformed server messages and keep the live connection open.
      }
    })
    this.socket.addEventListener('close', () => {
      this.socket = null
      if (!this.stopped) this.reconnectTimer = window.setTimeout(() => this.start(), 1500)
    })
  }

  stop() {
    this.stopped = true
    window.clearTimeout(this.reconnectTimer)
    this.socket?.close()
    this.socket = null
  }

  subscribe(listener) {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  send(value) {
    if (this.socket?.readyState === window.WebSocket.OPEN) this.socket.send(JSON.stringify(value))
  }
}

export const realtime = new RealtimeClient()
