import { shallowMount } from '@vue/test-utils'
import { describe, it, expect } from 'vitest'
import App from './App.vue'

describe('App', () => {
  function createWrapper() {
    return shallowMount(App, {
      global: {
        stubs: {
          RouterView: true,
          'el-config-provider': {
            props: ['locale'],
            template: '<div class="config-provider" :data-locale-name="locale.name"><slot /></div>',
          },
          'el-button': true,
          'el-result': true,
        },
      },
    })
  }

  it('renders without error', () => {
    const wrapper = createWrapper()
    expect(wrapper.exists()).toBe(true)
  })

  it('wraps content in ErrorFallback', () => {
    const wrapper = createWrapper()
    expect(wrapper.find('error-fallback-stub').exists()).toBe(true)
  })

  it('uses Chinese locale', () => {
    const wrapper = createWrapper()
    const configProvider = wrapper.find('.config-provider')
    expect(configProvider.attributes('data-locale-name')).toBe('zh-cn')
  })
})
