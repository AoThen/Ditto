<template>
  <div class="groups-page">
    <div class="page-header">
      <h2>分组管理</h2>
      <el-button type="primary" @click="showCreateDialog">创建分组</el-button>
    </div>

    <!-- Group Tree -->
    <el-card class="group-tree-card">
      <template #header>
        <div class="card-header">
          <span>分组列表</span>
          <el-input
            v-model="searchText"
            placeholder="搜索分组"
            clearable
            style="width: 200px"
            prefix-icon="Search"
          />
        </div>
      </template>

      <el-table :data="filteredGroups" style="width: 100%" row-key="id" :tree-props="{ children: 'children' }" :default-expand-all="true">
        <el-table-column prop="name" label="分组名称" min-width="200">
          <template #default="{ row }">
            <el-icon v-if="row.children && row.children.length > 0" style="margin-right: 4px"><Folder /></el-icon>
            <el-icon v-else style="margin-right: 4px"><Document /></el-icon>
            {{ row.name }}
          </template>
        </el-table-column>
        <el-table-column prop="description" label="描述" min-width="200" show-overflow-tooltip />
        <el-table-column prop="clip_count" label="剪贴板数量" width="120" align="center" />
        <el-table-column prop="created_at" label="创建时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.created_at) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button size="small" @click="showEditDialog(row)">编辑</el-button>
            <el-button size="small" type="danger" @click="handleDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>

      <el-empty v-if="filteredGroups.length === 0" description="暂无分组，点击上方「创建分组」开始" />
    </el-card>

    <!-- Create/Edit Dialog -->
    <el-dialog
      v-model="dialogVisible"
      :title="isEdit ? '编辑分组' : '创建分组'"
      width="500px"
      @closed="resetForm"
    >
      <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
        <el-form-item label="名称" prop="name">
          <el-input v-model="form.name" placeholder="请输入分组名称" />
        </el-form-item>
        <el-form-item label="描述" prop="description">
          <el-input v-model="form.description" type="textarea" :rows="3" placeholder="请输入分组描述（可选）" />
        </el-form-item>
        <el-form-item label="父分组" prop="parent_id">
          <el-select v-model="form.parent_id" placeholder="选择父分组（可选）" clearable style="width: 100%">
            <el-option
              v-for="item in parentGroupOptions"
              :key="item.id"
              :label="item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="submitLoading" @click="handleSubmit">
          {{ isEdit ? '保存' : '创建' }}
        </el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Folder, Document } from '@element-plus/icons-vue'
import { listGroups, createGroup, updateGroup, deleteGroup } from '@/api/groups'

const groups = ref([])
const searchText = ref('')
const dialogVisible = ref(false)
const isEdit = ref(false)
const submitLoading = ref(false)
const formRef = ref(null)

const form = ref({
  id: '',
  name: '',
  description: '',
  parent_id: null,
  clip_order: 0
})

const rules = {
  name: [{ required: true, message: '请输入分组名称', trigger: 'blur' }]
}

const filteredGroups = computed(() => {
  if (!searchText.value) return groups.value
  const text = searchText.value.toLowerCase()
  const filterTree = (items) => {
    return items.reduce((acc, item) => {
      const match = item.name.toLowerCase().includes(text) ||
                    (item.description && item.description.toLowerCase().includes(text))
      const filteredChildren = item.children ? filterTree(item.children) : []
      if (match || filteredChildren.length > 0) {
        acc.push({ ...item, children: filteredChildren })
      }
      return acc
    }, [])
  }
  return filterTree(groups.value)
})

const parentGroupOptions = computed(() => {
  // Flatten group list for select options, exclude current group when editing
  const flatten = (items) => {
    return items.reduce((acc, item) => {
      acc.push(item)
      if (item.children) acc.push(...flatten(item.children))
      return acc
    }, [])
  }
  return flatten(groups.value).filter(g => !isEdit.value || g.id !== form.value.id)
})

function formatTime(timeStr) {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

async function fetchGroups() {
  try {
    const res = await listGroups()
    if (res.code === 0) {
      // Build tree structure from flat list
      const flatList = res.data?.items || res.data || []
      const map = {}
      const roots = []
      flatList.forEach(g => {
        g.children = []
        map[g.id] = g
      })
      flatList.forEach(g => {
        if (g.parent_id && map[g.parent_id]) {
          map[g.parent_id].children.push(g)
        } else {
          roots.push(g)
        }
      })
      groups.value = roots
    }
  } catch (err) {
    ElMessage.error('获取分组列表失败: ' + err.message)
  }
}

function showCreateDialog() {
  isEdit.value = false
  form.value = { id: '', name: '', description: '', parent_id: null, clip_order: 0 }
  dialogVisible.value = true
}

function showEditDialog(group) {
  isEdit.value = true
  form.value = {
    id: group.id,
    name: group.name,
    description: group.description || '',
    parent_id: group.parent_id || null,
    clip_order: group.clip_order || 0
  }
  dialogVisible.value = true
}

function resetForm() {
  form.value = { id: '', name: '', description: '', parent_id: null, clip_order: 0 }
  if (formRef.value) formRef.value.resetFields()
}

async function handleSubmit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid) => {
    if (!valid) return
    submitLoading.value = true
    try {
      const data = {
        name: form.value.name,
        description: form.value.description,
        parent_id: form.value.parent_id || null,
        clip_order: form.value.clip_order
      }
      let res
      if (isEdit.value) {
        res = await updateGroup(form.value.id, data)
      } else {
        res = await createGroup(data)
      }
      if (res.code === 0) {
        ElMessage.success(isEdit.value ? '分组已更新' : '分组已创建')
        dialogVisible.value = false
        await fetchGroups()
      }
    } catch (err) {
      ElMessage.error((isEdit.value ? '更新' : '创建') + '分组失败: ' + err.message)
    } finally {
      submitLoading.value = false
    }
  })
}

async function handleDelete(group) {
  try {
    await ElMessageBox.confirm(
      `确定要删除分组「${group.name}」吗？${group.clip_count > 0 ? '分组内的剪贴板不会被删除，但会取消分组关联。' : ''}`,
      '确认删除',
      { type: 'warning' }
    )
    const res = await deleteGroup(group.id)
    if (res.code === 0) {
      ElMessage.success('分组已删除')
      await fetchGroups()
    }
  } catch (err) {
    if (err !== 'cancel') {
      ElMessage.error('删除分组失败: ' + err.message)
    }
  }
}

onMounted(() => {
  fetchGroups()
})
</script>

<style scoped>
.groups-page {
  padding: 20px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  font-size: 22px;
  font-weight: 600;
}

.group-tree-card {
  background: #fff;
  border-radius: 8px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
</style>
