<template>
  <el-tree
    :data="treeData"
    :props="{ children: 'children', label: 'label' }"
    node-key="id"
    :expand-on-click-node="false"
    :default-expanded-keys="expandedKeys"
    @node-click="handleNodeClick"
    @node-contextmenu="handleNodeContext"
  >
    <template #default="{ node, data }">
      <div class="group-tree-node">
        <span class="group-tree-node__label">{{ node.label }}</span>
        <span class="group-tree-node__count" v-if="data.count !== undefined">
          ({{ data.count }})
        </span>
        <span class="group-tree-node__actions">
          <el-button
            size="small"
            link
            type="primary"
            @click.stop="handleAddGroup(data)"
          >
            +
          </el-button>
        </span>
      </div>
    </template>
  </el-tree>

  <el-dropdown
    ref="contextMenu"
    trigger="contextmenu"
    :virtual-triggering="true"
    :virtual-ref="contextTarget"
  >
    <el-dropdown-menu>
      <el-dropdown-item @click="handleRename">重命名</el-dropdown-item>
      <el-dropdown-item @click="handleMoveToGroup">移动到分组</el-dropdown-item>
      <el-dropdown-item divided @click="handleDeleteGroup" type="danger">删除分组</el-dropdown-item>
    </el-dropdown-menu>
  </el-dropdown>
</template>

<script setup>
import { ref, computed } from 'vue'

const props = defineProps({
  groups: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['select', 'add', 'rename', 'delete', 'move'])

const expandedKeys = ref([])
const contextMenu = ref(null)
const contextTarget = ref(null)
const selectedNode = ref(null)

const treeData = computed(() => {
  // 转换为 el-tree 所需格式
  return props.groups.map(g => ({
    id: g.id,
    label: g.name || '未命名分组',
    count: g.clip_count,
    children: g.children || []
  }))
})

const handleNodeClick = (data) => {
  emit('select', data)
}

const handleNodeContext = (e, node, data) => {
  selectedNode.value = data
  contextTarget.value = {
    getBoundingClientRect: () => ({
      x: e.clientX,
      y: e.clientY,
      width: 0,
      height: 0,
      left: e.clientX,
      top: e.clientY,
      right: e.clientX,
      bottom: e.clientY
    })
  }
  contextMenu.value?.open()
}

const handleAddGroup = (parent) => {
  emit('add', parent)
}

const handleRename = () => {
  if (selectedNode.value) {
    emit('rename', selectedNode.value)
  }
}

const handleMoveToGroup = () => {
  if (selectedNode.value) {
    emit('move', selectedNode.value)
  }
}

const handleDeleteGroup = () => {
  if (selectedNode.value) {
    emit('delete', selectedNode.value)
  }
}
</script>

<style scoped>
.group-tree-node {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  padding-right: 8px;
}

.group-tree-node__label {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.group-tree-node__count {
  font-size: 12px;
  color: #999;
  margin-right: 8px;
}

.group-tree-node__actions {
  opacity: 0;
  transition: opacity 0.2s;
}

.group-tree-node:hover .group-tree-node__actions {
  opacity: 1;
}
</style>
