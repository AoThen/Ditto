import { mount } from '@vue/test-utils'
import { describe, it, expect, vi } from 'vitest'
import NotFound from './NotFound.vue'

const mockPush = vi.fn()
const mockBack = vi.fn()

vi.mock('vue-router', () => ({
  useRouter: () => ({
    push: mockPush,
    back: mockBack,
  }),
}))

describe('NotFound', () => {
  function createWrapper() {
    return mount(NotFound, {
      global: {
        stubs: {
          'el-result': {
            props: ['title', 'subTitle'],
            template: '<div><div class="title">{{ title }}</div><div class="sub-title">{{ subTitle }}</div><slot name="extra" /></div>',
          },
          'el-button': {
            template: '<button class="el-button"><slot /></button>',
          },
        },
      },
    })
  }

  it('renders 404 title', () => {
    const wrapper = createWrapper()
    expect(wrapper.text()).toContain('404')
  })

  it('renders subtitle', () => {
    const wrapper = createWrapper()
    expect(wrapper.text()).toContain('抱歉，您访问的页面不存在')
  })

  it('renders two buttons', () => {
    const wrapper = createWrapper()
    expect(wrapper.text()).toContain('返回首页')
    expect(wrapper.text()).toContain('返回上一页')
  })

  it('goHome calls router.push', () => {
    const wrapper = createWrapper()
    wrapper.vm.goHome()
    expect(mockPush).toHaveBeenCalledWith('/dashboard')
  })

  it('goBack calls router.back', () => {
    const wrapper = createWrapper()
    wrapper.vm.goBack()
    expect(mockBack).toHaveBeenCalled()
  })
})
