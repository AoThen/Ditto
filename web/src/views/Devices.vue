<template>
  <div class="devices-container">
    <div class="header">
      <h2>设备管理</h2>
      <el-button type="primary" @click="handleRefresh" :icon="Refresh">刷新</el-button>
    </div>

    <el-table
      :data="deviceList"
      v-loading="loading"
      style="width: 100%; margin-top: 20px"
    >
      <el-table-column prop="id" label="ID" width="80" />
      <el-table-column prop="device_id" label="设备标识" width="200" show-overflow-tooltip />
      <el-table-column prop="device_name" label="设备名称" width="200" show-overflow-tooltip />
      <el-table-column prop="last_seen" label="最后在线时间" width="200">
        <template #default="{ row }">
          {{ formatDate(row.last_seen) }}
        </template>
      </el-table-column>
      <el-table-column prop="created_at" label="注册时间" width="200">
        <template #default="{ row }">
          {{ formatDate(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column label="状态" width="120">
        <template #default="{ row }">
          <el-tag :type="isDeviceActive(row.last_seen) ? 'success' : 'info'" size="small">
            {{ isDeviceActive(row.last_seen) ? '在线' : '离线' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="120">
        <template #default="{ row }">
          <el-button
            type="danger"
            size="small"
            @click="handleRemoveDevice(row)"
            :disabled="row.is_current"
          >
            {{ row.is_current ? '当前设备' : '移除' }}
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-empty v-if="!loading && deviceList.length === 0" description="暂无设备记录" />
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { listDevices, removeDevice } from '@/api/devices'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Refresh } from '@element-plus/icons-vue'
import { useUserStore } from '@/stores/user'
import { formatDate } from '@/composables/useFormatDate'

const deviceList = ref([])
const loading = ref(false)
const userStore = useUserStore()

function isDeviceActive(lastSeen) {
  if (!lastSeen) return false
  const lastSeenDate = new Date(lastSeen)
  const now = new Date()
  // Consider active if seen within last 5 minutes
  return (now - lastSeenDate) < 5 * 60 * 1000
}

async function fetchDevices() {
  loading.value = true
  try {
    const res = await listDevices()
    if (res.code === 0) {
      // Mark current device (based on stored token)
      const currentDeviceId = userStore.deviceId
      deviceList.value = (res.data?.items || res.data || []).map(device => ({
        ...device,
        is_current: device.device_id === currentDeviceId
      }))
    } else {
      deviceList.value = []
    }
  } catch (err) {
    console.error('Failed to fetch devices:', err)
    ElMessage.error('获取设备列表失败')
    deviceList.value = []
  } finally {
    loading.value = false
  }
}

function handleRefresh() {
  fetchDevices()
}

async function handleRemoveDevice(row) {
  if (row.is_current) {
    ElMessage.warning('不能移除当前设备')
    return
  }

  try {
    await ElMessageBox.confirm(
      `确定要移除设备 "${row.device_name}" 吗？该设备将无法同步数据。`,
      '确认移除设备',
      {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning',
      }
    )

    await removeDevice(row.id)
    ElMessage.success('设备移除成功')
    fetchDevices()
  } catch (err) {
    if (err !== 'cancel') {
      console.error('Failed to remove device:', err)
      ElMessage.error('设备移除失败')
    }
  }
}

onMounted(() => {
  fetchDevices()
})
</script>

<style scoped>
.devices-container {
  padding: 20px;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.header h2 {
  margin: 0;
  color: #303133;
  font-size: 20px;
}
</style>
