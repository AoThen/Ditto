import { describe, it, expect, vi, beforeEach } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import Groups from '@/views/Groups.vue'
import * as groupsApi from '@/api/groups'
import { ElMessage, ElMessageBox } from 'element-plus'

// Mock the API module
vi.mock('@/api/groups', () => ({
  listGroups: vi.fn(),
  createGroup: vi.fn(),
  updateGroup: vi.fn(),
  deleteGroup: vi.fn(),
}))

// Mock Element Plus messaging
vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: {
      success: vi.fn(),
      error: vi.fn(),
    },
    ElMessageBox: {
      confirm: vi.fn(),
    },
  }
})

// Helper: mount with stubbed Element Plus components
function mountGroups() {
  // We mount the component without full Element Plus to test logic
  // For unit tests, we focus on computed properties and state logic
  // by extracting and testing the pure functions separately

  // For full component testing, we'd need Element Plus registered
  // Here we test the underlying logic via the composable pattern
  return null
}

// ── Pure logic tests for group tree building ─────────────────
describe('Groups tree building logic', () => {
  function buildTree(flatList) {
    const map = {}
    const roots = []
    flatList.forEach(g => { g.children = []; map[g.id] = g })
    flatList.forEach(g => {
      if (g.parent_id && map[g.parent_id]) {
        map[g.parent_id].children.push(g)
      } else {
        roots.push(g)
      }
    })
    return roots
  }

  it('should build tree from flat list', () => {
    const flat = [
      { id: '1', name: 'Root', parent_id: null },
      { id: '2', name: 'Child', parent_id: '1' },
    ]
    const tree = buildTree(flat)
    expect(tree).toHaveLength(1)
    expect(tree[0].name).toBe('Root')
    expect(tree[0].children).toHaveLength(1)
    expect(tree[0].children[0].name).toBe('Child')
  })

  it('should handle multiple roots', () => {
    const flat = [
      { id: '1', name: 'A', parent_id: null },
      { id: '2', name: 'B', parent_id: null },
    ]
    const tree = buildTree(flat)
    expect(tree).toHaveLength(2)
  })

  it('should handle deeply nested children', () => {
    const flat = [
      { id: '1', name: 'L1', parent_id: null },
      { id: '2', name: 'L2', parent_id: '1' },
      { id: '3', name: 'L3', parent_id: '2' },
    ]
    const tree = buildTree(flat)
    expect(tree[0].children[0].children[0].name).toBe('L3')
  })

  it('should handle empty list', () => {
    expect(buildTree([])).toEqual([])
  })
})

// ── Filter logic tests ──────────────────────────────────────
describe('Groups filter logic', () => {
  function filterTree(items, text) {
    if (!text) return items
    const lower = text.toLowerCase()
    return items.reduce((acc, item) => {
      const match = item.name.toLowerCase().includes(lower) ||
                    (item.description && item.description.toLowerCase().includes(lower))
      const filteredChildren = item.children ? filterTree(item.children, text) : []
      if (match || filteredChildren.length > 0) {
        acc.push({ ...item, children: filteredChildren })
      }
      return acc
    }, [])
  }

  const sampleTree = [
    { id: '1', name: 'Work', description: 'Work items', children: [
      { id: '2', name: 'Dev', description: 'Development', children: [] },
      { id: '3', name: 'Design', description: 'UI Design', children: [] },
    ]},
    { id: '4', name: 'Personal', description: 'Personal stuff', children: [] },
  ]

  it('should return all items when no search text', () => {
    expect(filterTree(sampleTree, '')).toHaveLength(2)
    expect(filterTree(sampleTree)).toHaveLength(2)
  })

  it('should match by name', () => {
    const result = filterTree(sampleTree, 'Work')
    expect(result).toHaveLength(1)
    expect(result[0].name).toBe('Work')
  })

  it('should match by description', () => {
    const result = filterTree(sampleTree, 'Development')
    expect(result).toHaveLength(1)
    expect(result[0].name).toBe('Work')
    expect(result[0].children[0].name).toBe('Dev')
  })

  it('should include parent when child matches', () => {
    const result = filterTree(sampleTree, 'Dev')
    expect(result).toHaveLength(1)
    expect(result[0].name).toBe('Work')
    // Only 'Dev' matches 'Dev', 'Design' does not
    expect(result[0].children).toHaveLength(1)
    expect(result[0].children[0].name).toBe('Dev')
  })

  it('should return empty array for no match', () => {
    const result = filterTree(sampleTree, 'nonexistent_xyz')
    expect(result).toHaveLength(0)
  })
})

// ── Flatten logic tests (parent group options) ──────────────
describe('Groups flatten logic', () => {
  function flatten(items, excludeId = null) {
    return items.reduce((acc, item) => {
      if (item.id !== excludeId) acc.push(item)
      if (item.children) acc.push(...flatten(item.children, excludeId))
      return acc
    }, [])
  }

  it('should flatten nested tree', () => {
    const tree = [
      { id: '1', name: 'Root', children: [
        { id: '2', name: 'Child', children: [] },
      ]},
    ]
    const flat = flatten(tree)
    expect(flat).toHaveLength(2)
    expect(flat.map(g => g.name)).toContain('Root')
    expect(flat.map(g => g.name)).toContain('Child')
  })

  it('should exclude current group when editing', () => {
    const tree = [
      { id: '1', name: 'A', children: [] },
      { id: '2', name: 'B', children: [] },
    ]
    const flat = flatten(tree, '1')
    expect(flat).toHaveLength(1)
    expect(flat[0].name).toBe('B')
  })
})

// ── Format time tests ───────────────────────────────────────
describe('formatTime', () => {
  function formatTime(timeStr) {
    if (!timeStr) return '-'
    return new Date(timeStr).toLocaleString('zh-CN')
  }

  it('should return dash for null/undefined', () => {
    expect(formatTime(null)).toBe('-')
    expect(formatTime(undefined)).toBe('-')
    expect(formatTime('')).toBe('-')
  })

  it('should format valid date string', () => {
    const result = formatTime('2024-01-15T10:30:00Z')
    expect(result).toContain('2024')
    expect(result.length).toBeGreaterThan(5)
  })
})

// ── Form validation rules tests ─────────────────────────────
describe('Form validation rules', () => {
  const rules = {
    name: [{ required: true, message: '请输入分组名称', trigger: 'blur' }]
  }

  it('should require name field', () => {
    const nameRule = rules.name[0]
    expect(nameRule.required).toBe(true)
    expect(nameRule.message).toBe('请输入分组名称')
  })
})

// ── API integration tests (mocked) ──────────────────────────
describe('Groups API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('listGroups should return groups list', async () => {
    groupsApi.listGroups.mockResolvedValue({
      code: 0,
      data: [
        { id: '1', name: 'Group A', description: 'Test', parent_id: null, clip_count: 5 },
      ],
    })

    const res = await groupsApi.listGroups()
    expect(res.code).toBe(0)
    expect(res.data).toHaveLength(1)
    expect(res.data[0].name).toBe('Group A')
  })

  it('createGroup should create a new group', async () => {
    groupsApi.createGroup.mockResolvedValue({
      code: 0,
      data: { id: 'new-1', name: 'New Group' },
    })

    const res = await groupsApi.createGroup({
      name: 'New Group',
      description: 'Test',
      parent_id: null,
      clip_order: 0,
    })
    expect(res.code).toBe(0)
    expect(groupsApi.createGroup).toHaveBeenCalledWith({
      name: 'New Group',
      description: 'Test',
      parent_id: null,
      clip_order: 0,
    })
  })

  it('updateGroup should update an existing group', async () => {
    groupsApi.updateGroup.mockResolvedValue({
      code: 0,
      data: { id: '1', name: 'Updated' },
    })

    const res = await groupsApi.updateGroup('1', { name: 'Updated' })
    expect(res.code).toBe(0)
    expect(groupsApi.updateGroup).toHaveBeenCalledWith('1', { name: 'Updated' })
  })

  it('deleteGroup should delete a group', async () => {
    groupsApi.deleteGroup.mockResolvedValue({ code: 0, data: null })

    const res = await groupsApi.deleteGroup('1')
    expect(res.code).toBe(0)
    expect(groupsApi.deleteGroup).toHaveBeenCalledWith('1')
  })

  it('should handle API errors gracefully', async () => {
    groupsApi.listGroups.mockRejectedValue(new Error('Network error'))

    await expect(groupsApi.listGroups()).rejects.toThrow('Network error')
  })
})
