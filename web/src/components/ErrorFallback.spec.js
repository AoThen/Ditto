import { mount } from '@vue/test-utils'
import { describe, it, expect, vi } from 'vitest'
import ErrorFallback from './ErrorFallback.vue'

describe('ErrorFallback', () => {
  it('mounts with hasError defaulting to false', () => {
    const wrapper = mount(ErrorFallback, {
      slots: { default: '<p>正常内容</p>' },
      global: { stubs: ['el-result', 'el-button'] },
    })
    expect(wrapper.vm.hasError).toBe(false)
  })

  it('renders slot content when no error', () => {
    const wrapper = mount(ErrorFallback, {
      slots: { default: '<p>正常内容</p>' },
      global: { stubs: ['el-result', 'el-button'] },
    })
    expect(wrapper.text()).toContain('正常内容')
    expect(wrapper.find('.error-fallback').exists()).toBe(false)
  })

  it('shows error UI when error is captured', async () => {
    const wrapper = mount(ErrorFallback, {
      slots: { default: '<p>正常内容</p>' },
      global: { stubs: ['el-result', 'el-button'] },
    })
    wrapper.vm.hasError = true
    await wrapper.vm.$nextTick()
    expect(wrapper.find('.error-fallback').exists()).toBe(true)
    expect(wrapper.text()).not.toContain('正常内容')
  })

  it('handleRetry resets error and reloads page', async () => {
    const reloadSpy = vi.spyOn(window.location, 'reload').mockImplementation(() => {})

    const wrapper = mount(ErrorFallback, {
      slots: { default: '<p>正常内容</p>' },
      global: { stubs: ['el-result', 'el-button'] },
    })
    wrapper.vm.hasError = true
    await wrapper.vm.$nextTick()

    wrapper.vm.handleRetry()
    expect(wrapper.vm.hasError).toBe(false)
    expect(reloadSpy).toHaveBeenCalledTimes(1)

    reloadSpy.mockRestore()
  })
})
