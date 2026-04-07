<template>
  <div class="clips-container">
    <div class="toolbar">
      <div class="ws-status" :class="{ connected: ws.isConnected, disconnected: !ws.isConnected }">
        {{ ws.isConnected ? '● 实时同步中' : '○ 未连接' }}
      </div>
      <el-input
        v-model="searchQuery"
        placeholder="搜索剪贴板内容..."
        clearable
        style="width: 300px"
        @clear="handleSearch"
        @keyup.enter="handleSearch"
      >
        <template #append>
          <el-button @click="handleSearch">搜索</el-button>
        </template>
      </el-input>
      <el-button type="primary" @click="handleRefresh">刷新</el-button>
      <el-button type="danger" :disabled="!selectedRows.length" @click="handleBatchDelete">
        删除选中 ({{ selectedRows.length }})
      </el-button>
    </div>

    <el-table
      :data="clipList"
      v-loading="loading"
      @selection-change="handleSelectionChange"
      @row-click="handleRowClick"
      style="width: 100%; margin-top: 16px"
    >
      <el-table-column type="selection" width="55" />
      <el-table-column prop="description" label="描述" show-overflow-tooltip />
      <el-table-column prop="paste_count" label="粘贴次数" width="100" />
      <el-table-column prop="created_at" label="创建时间" width="180">
        <template #default="{ row }">
          {{ formatDate(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="100">
        <template #default="{ row }">
          <el-button type="danger" size="small" @click.stop="handleDelete(row.id)">
            删除
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-pagination
      v-model:current-page="currentPage"
      :page-size="20"
      :total="total"
      layout="total, prev, pager, next"
      style="margin-top: 16px; justify-content: center"
      @current-change="handlePageChange"
    />

    <!-- Clip Detail Dialog -->
    <el-dialog v-model="detailVisible" title="剪贴板详情" width="600px">
      <div v-if="currentClip" class="clip-detail">
        <p><strong>描述:</strong> {{ currentClip.description || '(无)' }}</p>
        <p><strong>创建时间:</strong> {{ formatDate(currentClip.created_at) }}</p>
        <p><strong>粘贴次数:</strong> {{ currentClip.paste_count }}</p>
        <div v-if="currentClip.formats && currentClip.formats.length">
          <strong>格式:</strong>
          <div v-for="(fmt, idx) in currentClip.formats" :key="idx" class="format-item">
            <span class="format-icon">{{ getFormatIcon(fmt.format_type) }}</span>
            <span class="format-name">{{ getFormatName(fmt.format_type) }}</span>
            <span class="format-size">({{ fmt.data_size }} 字节)</span>
          </div>
        </div>
        <el-alert v-else title="无格式数据" type="info" />
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { listClips, getClip, deleteClip, batchDeleteClips } from '@/api/clips'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useWebSocket } from '@/composables/useWebSocket'

const ws = useWebSocket()

// Listen for WS clip notifications
function onWsClipAdded(event) {
  console.log('[Clips] WS clip added:', event.detail)
  fetchClips()
  ElMessage.success(`收到来自 ${event.detail.device_id} 的新剪贴板`)
}

const clipList = ref([])
const loading = ref(false)
const searchQuery = ref('')
const currentPage = ref(1)
const total = ref(0)
const selectedRows = ref([])
const detailVisible = ref(false)
const currentClip = ref(null)

function formatDate(dateStr) {
  if (!dateStr) return '-'
  return new Date(dateStr).toLocaleString('zh-CN')
}

function getFormatIcon(formatType) {
  const iconMap = {
    'text': '\u{1F4DD}',
    'unicode': '\u{1F310}',
    'file': '\u{1F4C1}',
  }
  return iconMap[formatType] || '\u{1F4DD}'
}

function getFormatName(formatType) {
  const nameMap = {
    'text': '文本',
    'unicode': 'Unicode',
    'file': '文件路径',
  }
  return nameMap[formatType] || formatType
}

async function fetchClips() {
  loading.value = true
  try {
    const params = {
      page: currentPage.value,
      per_page: 20,
    }
    if (searchQuery.value) {
      params.search = searchQuery.value
    }
    const res = await listClips(params)
    if (res.code === 200) {
      clipList.value = res.data.items || res.data || []
      total.value = res.data.total || 0
    } else {
      clipList.value = res.data?.items || res.data || []
      total.value = res.data?.total || 0
    }
  } catch (err) {
    console.error('Failed to fetch clips:', err)
    ElMessage.error('获取剪贴板列表失败')
    clipList.value = []
    total.value = 0
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  currentPage.value = 1
  fetchClips()
}

function handleRefresh() {
  fetchClips()
}

function handlePageChange(page) {
  currentPage.value = page
  fetchClips()
}

function handleSelectionChange(selection) {
  selectedRows.value = selection
}

function handleRowClick(row) {
  openClipDetail(row.id)
}

async function openClipDetail(id) {
  try {
    const res = await getClip(id)
    currentClip.value = res.data || res
    detailVisible.value = true
  } catch (err) {
    console.error('Failed to fetch clip detail:', err)
    ElMessage.error('获取剪贴板详情失败')
  }
}

async function handleDelete(id) {
  try {
    await ElMessageBox.confirm('确定删除此剪贴板？', '确认删除', {
      confirmButtonText: '确定',
      cancelButtonText: '取消',
      type: 'warning',
    })
    await deleteClip(id)
    ElMessage.success('删除成功')
    fetchClips()
  } catch (err) {
    if (err !== 'cancel') {
      console.error('Failed to delete clip:', err)
      ElMessage.error('删除剪贴板失败')
    }
  }
}

async function handleBatchDelete() {
  if (!selectedRows.value.length) return
  try {
    await ElMessageBox.confirm(
      `确定删除选中的 ${selectedRows.value.length} 个剪贴板？`,
      '确认删除',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning',
      }
    )
    const ids = selectedRows.value.map(row => row.id)
    await batchDeleteClips(ids)
    ElMessage.success('批量删除成功')
    selectedRows.value = []
    fetchClips()
  } catch (err) {
    if (err !== 'cancel') {
      console.error('Failed to batch delete clips:', err)
      ElMessage.error('批量删除剪贴板失败')
    }
  }
}

onMounted(() => {
  ws.connect()
  window.addEventListener('ws-clip-added', onWsClipAdded)
  fetchClips()
})

onUnmounted(() => {
  ws.disconnect()
  window.removeEventListener('ws-clip-added', onWsClipAdded)
})
</script>

<style scoped>
.clips-container {
  padding: 20px;
}

.toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
}

.ws-status {
  font-size: 12px;
  padding: 4px 8px;
  border-radius: 4px;
  white-space: nowrap;
}

.ws-status.connected {
  color: #67c23a;
  background: #f0f9eb;
}

.ws-status.disconnected {
  color: #f56c6c;
  background: #fef0f0;
}

.clip-detail p {
  margin: 8px 0;
}

.format-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;
}

.format-icon {
  font-size: 16px;
}

.format-name {
  font-weight: 500;
}

.format-size {
  color: #909399;
  font-size: 12px;
}
</style>
