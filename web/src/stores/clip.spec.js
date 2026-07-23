import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useClipStore } from '@/stores/clip'

describe('clip store', () => {
  let store

  beforeEach(() => {
    setActivePinia(createPinia())
    store = useClipStore()
  })

  it('lastNotifiedClip defaults to null', () => {
    expect(store.lastNotifiedClip).toBeNull()
  })

  it('notifyClipAdded updates lastNotifiedClip with the given data', () => {
    const data = { clipId: '123', content: 'hello' }
    store.notifyClipAdded(data)
    expect(store.lastNotifiedClip).toEqual(data)
  })

  it('notifyClipAdded dispatches a CustomEvent with type ws-clip-added', () => {
    const spy = vi.spyOn(window, 'dispatchEvent')
    const data = { clipId: '456' }

    store.notifyClipAdded(data)

    expect(spy).toHaveBeenCalledTimes(1)
    const event = spy.mock.calls[0][0]
    expect(event).toBeInstanceOf(CustomEvent)
    expect(event.type).toBe('ws-clip-added')
    expect(event.detail).toEqual(data)

    spy.mockRestore()
  })

  it('multiple calls override previous lastNotifiedClip', () => {
    store.notifyClipAdded({ id: 1 })
    store.notifyClipAdded({ id: 2 })
    store.notifyClipAdded({ id: 3 })

    expect(store.lastNotifiedClip).toEqual({ id: 3 })
  })
})