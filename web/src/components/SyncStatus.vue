<template>
  <div class="sync-status" :class="statusClass">
    <el-tooltip :content="tooltipText" placement="top">
      <div class="sync-status__indicator">
        <el-icon v-if="status === 'syncing'" class="is-loading"><Loading /></el-icon>
        <el-icon v-else-if="status === 'success'"><CircleCheck /></el-icon>
        <el-icon v-else-if="status === 'error'"><CircleClose /></el-icon>
        <el-icon v-else><InfoFilled /></el-icon>
        <span class="sync-status__text">{{ statusText }}</span>
      </div>
    </el-tooltip>

    <el-popover
      v-if="showDetails"
      trigger="click"
      width="300"
    >
      <template #reference>
        <el-button size="small" link>详情</el-button>
      </template>
      <div class="sync-details">
        <p>最后同步：{{ lastSyncTime }}</p>
        <p>已同步：{{ syncedCount }} 条</p>
        <p>跳过：{{ skippedCount }} 条</p>
        <p v-if="error" class="sync-details__error">错误：{{ error }}</p>
      </div>
    </el-popover>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { Loading, CircleCheck, CircleClose, InfoFilled } from '@element-plus/icons-vue'

const props = defineProps({
  status: {
    type: String,
    default: 'idle', // idle | syncing | success | error
  },
  lastSyncTime: {
    type: String,
    default: '未同步'
  },
  syncedCount: {
    type: Number,
    default: 0
  },
  skippedCount: {
    type: Number,
    default: 0
  },
  error: {
    type: String,
    default: ''
  },
  showDetails: {
    type: Boolean,
    default: true
  }
})

const statusClass = computed(() => `sync-status--${props.status}`)

const statusText = computed(() => {
  const map = {
    idle: '待同步',
    syncing: '同步中...',
    success: '同步成功',
    error: '同步失败'
  }
  return map[props.status] || '未知'
})

const tooltipText = computed(() => {
  if (props.error) return props.error
  return `最后同步：${props.lastSyncTime}`
})
</script>

<style scoped>
.sync-status {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.sync-status__indicator {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 12px;
}

.sync-status__text {
  margin-left: 4px;
}

.sync-status--idle .sync-status__indicator {
  color: #999;
}

.sync-status--syncing .sync-status__indicator {
  color: #409eff;
}

.sync-status--success .sync-status__indicator {
  color: #67c23a;
}

.sync-status--error .sync-status__indicator {
  color: #f56c6c;
}

.sync-details {
  font-size: 13px;
  line-height: 1.6;
}

.sync-details__error {
  color: #f56c6c;
  margin-top: 8px;
}
</style>
