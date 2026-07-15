<template>
  <div class="sync-logs-container">
    <el-card shadow="hover">
      <template #header>
        <div class="card-header">
          <span>同步日志</span>
        </div>
      </template>

      <el-form :inline="true" class="filter-form">
        <el-form-item label="操作类型">
          <el-select v-model="filterAction" clearable placeholder="全部操作" @change="fetchLogs">
            <el-option label="全部" value="" />
            <el-option label="推送" value="push" />
            <el-option label="拉取" value="pull" />
            <el-option label="删除" value="delete" />
          </el-select>
        </el-form-item>
      </el-form>

      <el-table :data="logs" v-loading="loading" stripe style="width: 100%">
        <el-table-column prop="id" label="ID" width="60" />
        <el-table-column label="操作" width="100">
          <template #default="{ row }">
            <el-tag :type="getActionTagType(row.action)" size="small">
              {{ getActionLabel(row.action) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="100">
          <template #default="{ row }">
            <el-tag :type="row.status === 'success' ? 'success' : 'danger'" size="small">
              {{ row.status === 'success' ? '成功' : '失败' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="device_id" label="设备ID" width="180" />
        <el-table-column prop="clip_count" label="剪贴板数" width="100" />
        <el-table-column prop="error" label="错误信息" min-width="200" show-overflow-tooltip />
        <el-table-column prop="synced_at" label="时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.synced_at) }}
          </template>
        </el-table-column>
      </el-table>

      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :total="total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @current-change="fetchLogs"
        @size-change="fetchLogs"
        style="margin-top: 20px; justify-content: flex-end"
      />
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { getSyncLogs } from '@/api/stats'
import { ElMessage } from 'element-plus'

const logs = ref([])
const loading = ref(false)
const total = ref(0)
const currentPage = ref(1)
const pageSize = ref(20)
const filterAction = ref('')

async function fetchLogs() {
  loading.value = true
  try {
    const res = await getSyncLogs({
      page: currentPage.value,
      per_page: pageSize.value,
      action: filterAction.value || undefined,
    })
    if (res.code === 0) {
      logs.value = res.data.items || []
      total.value = res.data.total || 0
    }
  } catch (err) {
    console.error('Failed to fetch sync logs:', err)
    ElMessage.error('获取同步日志失败')
  } finally {
    loading.value = false
  }
}

function formatTime(timeStr) {
  if (!timeStr) return '-'
  const date = new Date(timeStr)
  return date.toLocaleString('zh-CN', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  })
}

function getActionTagType(action) {
  switch (action) {
    case 'push': return 'primary'
    case 'pull': return 'success'
    case 'delete': return 'danger'
    default: return 'info'
  }
}

function getActionLabel(action) {
  switch (action) {
    case 'push': return '推送'
    case 'pull': return '拉取'
    case 'delete': return '删除'
    default: return action
  }
}

onMounted(() => {
  fetchLogs()
})
</script>

<style scoped>
.sync-logs-container {
  padding: 20px;
}

.filter-form {
  margin-bottom: 16px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-weight: 600;
  font-size: 16px;
}
</style>
