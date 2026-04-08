<template>
  <div class="dashboard-container">
    <h2 class="page-title">数据总览</h2>

    <!-- Stats Cards -->
    <el-row :gutter="20" class="stats-cards">
      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background: #409eff;">
            <el-icon :size="32"><Document /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.total_clips || 0 }}</div>
            <div class="stat-label">剪贴板总数</div>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background: #67c23a;">
            <el-icon :size="32"><CirclePlus /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.today_clips || 0 }}</div>
            <div class="stat-label">今日新增</div>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background: #e6a23c;">
            <el-icon :size="32"><Monitor /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.total_devices || 0 }}</div>
            <div class="stat-label">设备数量</div>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="stat-card">
          <div class="stat-icon" style="background: #f56c6c;">
            <el-icon :size="32"><Coin /></el-icon>
          </div>
          <div class="stat-info">
            <div class="stat-value">{{ stats.storage_mb?.toFixed(2) || '0.00' }}</div>
            <div class="stat-label">存储使用 (MB)</div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- Storage Usage Bar -->
    <el-card class="storage-card">
      <template #header>
        <div class="card-header">
          <span>存储使用情况</span>
          <span>{{ stats.storage_mb?.toFixed(2) || '0.00' }} / {{ stats.max_storage_mb || 100 }} MB</span>
        </div>
      </template>
      <el-progress
        :percentage="storagePercentage"
        :color="storageColor"
        :stroke-width="24"
        :show-text="true"
      />
    </el-card>

    <!-- Trend Chart -->
    <el-card class="trend-card">
      <template #header>
        <div class="card-header">
          <span>近 7 天趋势</span>
          <el-button type="primary" size="small" @click="fetchStats" :icon="Refresh">刷新</el-button>
        </div>
      </template>
      <div v-if="stats.trend && stats.trend.length > 0" class="trend-chart">
        <div class="chart-container">
          <div v-for="(item, idx) in stats.trend" :key="idx" class="bar-wrapper">
            <div class="bar" :style="{ height: getBarHeight(item.count) + '%' }">
              <span class="bar-label">{{ item.count }}</span>
            </div>
            <div class="bar-date">{{ formatDate(item.date) }}</div>
          </div>
        </div>
      </div>
      <el-empty v-else description="暂无趋势数据" />
    </el-card>

    <!-- Recent Clips -->
    <el-card class="recent-clips-card">
      <template #header>
        <div class="card-header">
          <span>最近同步的剪贴板</span>
          <el-button type="primary" size="small" @click="$router.push('/dashboard/clips')">查看全部</el-button>
        </div>
      </template>
      <el-table :data="recentClips" v-loading="loadingClips" style="width: 100%">
        <el-table-column prop="description" label="描述" show-overflow-tooltip />
        <el-table-column prop="source_device" label="来源设备" width="150" show-overflow-tooltip />
        <el-table-column prop="created_at" label="创建时间" width="180">
          <template #default="{ row }">
            {{ formatDateTime(row.created_at) }}
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { getStatsOverview } from '@/api/stats'
import { listClips } from '@/api/clips'
import { ElMessage } from 'element-plus'
import { Document, CirclePlus, Monitor, Coin, Refresh } from '@element-plus/icons-vue'

const stats = ref({
  total_clips: 0,
  today_clips: 0,
  total_devices: 0,
  storage_mb: 0,
  max_storage_mb: 100,
  trend: []
})

const recentClips = ref([])
const loadingClips = ref(false)

const storagePercentage = computed(() => {
  const max = stats.value.max_storage_mb || 100
  const used = stats.value.storage_mb || 0
  return Math.min((used / max) * 100, 100)
})

const storageColor = computed(() => {
  const pct = storagePercentage.value
  if (pct < 50) return '#67c23a'
  if (pct < 80) return '#e6a23c'
  return '#f56c6c'
})

function formatDate(dateStr) {
  if (!dateStr) return '-'
  const date = new Date(dateStr)
  return `${date.getMonth() + 1}/${date.getDate()}`
}

function formatDateTime(dateStr) {
  if (!dateStr) return '-'
  return new Date(dateStr).toLocaleString('zh-CN')
}

function getBarHeight(count) {
  const maxCount = Math.max(...stats.value.trend.map(t => t.count), 1)
  return Math.max((count / maxCount) * 80, 5) // Minimum 5% height
}

async function fetchStats() {
  try {
    const res = await getStatsOverview()
    if (res.code === 200 || res.code === 0) {
      stats.value = res.data || res
    }
  } catch (err) {
    console.error('Failed to fetch stats:', err)
    ElMessage.error('获取统计数据失败')
  }
}

async function fetchRecentClips() {
  loadingClips.value = true
  try {
    const res = await listClips({ page: 1, per_page: 5 })
    if (res.code === 200 || res.code === 0) {
      recentClips.value = res.data?.items || res.data || []
    }
  } catch (err) {
    console.error('Failed to fetch recent clips:', err)
    recentClips.value = []
  } finally {
    loadingClips.value = false
  }
}

onMounted(() => {
  fetchStats()
  fetchRecentClips()
})
</script>

<style scoped>
.dashboard-container {
  padding: 20px;
}

.page-title {
  margin: 0 0 24px 0;
  color: #303133;
  font-size: 20px;
}

.stats-cards {
  margin-bottom: 24px;
}

.stat-card {
  display: flex;
  align-items: center;
  padding: 8px;
}

.stat-card :deep(.el-card__body) {
  display: flex;
  align-items: center;
  width: 100%;
  padding: 20px;
}

.stat-icon {
  width: 64px;
  height: 64px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  margin-right: 16px;
  flex-shrink: 0;
}

.stat-info {
  flex: 1;
}

.stat-value {
  font-size: 28px;
  font-weight: 700;
  color: #303133;
  line-height: 1.2;
}

.stat-label {
  font-size: 14px;
  color: #909399;
  margin-top: 4px;
}

.storage-card {
  margin-bottom: 24px;
}

.trend-card {
  margin-bottom: 24px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.trend-chart {
  padding: 20px 0;
}

.chart-container {
  display: flex;
  align-items: flex-end;
  justify-content: space-around;
  height: 250px;
  padding: 20px;
  background: #fafafa;
  border-radius: 8px;
}

.bar-wrapper {
  display: flex;
  flex-direction: column;
  align-items: center;
  flex: 1;
  height: 100%;
  justify-content: flex-end;
}

.bar {
  width: 40px;
  background: linear-gradient(180deg, #409eff 0%, #66b1ff 100%);
  border-radius: 4px 4px 0 0;
  display: flex;
  align-items: flex-start;
  justify-content: center;
  padding-top: 8px;
  transition: height 0.3s ease;
  min-height: 20px;
}

.bar-label {
  color: white;
  font-size: 12px;
  font-weight: 600;
}

.bar-date {
  margin-top: 8px;
  font-size: 12px;
  color: #606266;
  text-align: center;
}

.recent-clips-card {
  margin-bottom: 24px;
}
</style>
