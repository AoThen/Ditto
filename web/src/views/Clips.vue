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
    <el-dialog v-model="detailVisible" title="剪贴板详情" width="800px" top="5vh">
      <div v-if="currentClip" class="clip-detail">
        <el-descriptions :column="2" border>
          <el-descriptions-item label="描述">{{ currentClip.description || '(无)' }}</el-descriptions-item>
          <el-descriptions-item label="创建时间">{{ formatDate(currentClip.created_at) }}</el-descriptions-item>
          <el-descriptions-item label="粘贴次数">{{ currentClip.paste_count }}</el-descriptions-item>
          <el-descriptions-item label="来源设备">{{ currentClip.source_device || currentClip.device_name || '(未知)' }}</el-descriptions-item>
        </el-descriptions>

        <div class="formats-section" style="margin-top: 20px;">
          <h4>格式数据 ({{ currentClip.formats?.length || 0 }})</h4>
          <el-tabs v-model="activeFormatTab" type="card">
            <el-tab-pane
              v-for="(fmt, idx) in currentClip.formats"
              :key="idx"
              :label="getFormatLabel(fmt.format_type)"
              :name="idx"
            >
              <div class="format-header-row">
                <div class="format-info">
                  <span class="format-name">{{ getFormatName(fmt.format_type) }}</span>
                  <span class="format-size">({{ fmt.data_size }} 字节)</span>
                </div>
                <el-button size="small" type="primary" @click="handleDownloadFormat(currentClip.id, fmt.format_type)">
                  <el-icon><Download /></el-icon> 下载原始数据
                </el-button>
              </div>

              <div class="format-preview">
                <!-- Text Preview -->
                <div v-if="isTextFormat(fmt.format_type)" class="text-preview">
                  <pre class="text-content">{{ decodeTextData(fmt) }}</pre>
                </div>

                <!-- HTML Preview -->
                <div v-else-if="isHTMLFormat(fmt.format_type)" class="html-preview">
                  <iframe
                    :srcdoc="decodeHTMLData(fmt)"
                    sandbox="allow-same-origin"
                    style="width: 100%; height: 400px; border: 1px solid #dcdfe6; border-radius: 4px;"
                  ></iframe>
                </div>

                <!-- Image Preview -->
                <div v-else-if="isImageFormat(fmt.format_type)" class="image-preview">
                  <el-image
                    v-if="fmt.data_base64"
                    :src="'data:image/png;base64,' + fmt.data_base64"
                    fit="contain"
                    style="max-height: 400px;"
                    :preview-src-list="['data:image/png;base64,' + fmt.data_base64]"
                  />
                  <el-alert v-else title="图片数据不可预览（需要 base64 编码）" type="info" :closable="false" />
                </div>

                <!-- File Path Preview -->
                <div v-else-if="isFileFormat(fmt.format_type)" class="file-preview">
                  <ul>
                    <li v-for="(path, pidx) in decodeFilePaths(fmt)" :key="pidx">{{ path }}</li>
                  </ul>
                </div>

                <!-- Raw Binary Preview (hex) -->
                <div v-else class="raw-preview">
                  <el-alert
                    :title="`二进制数据 (${fmt.data_size} 字节，${fmt.encoding || 'base64'} 编码)`"
                    type="info"
                    :closable="false"
                  />
                  <pre class="hex-content" v-if="fmt.data">{{ fmt.data.substring(0, 200) }}{{ fmt.data.length > 200 ? '...' : '' }}</pre>
                </div>
              </div>
            </el-tab-pane>
          </el-tabs>
        </div>
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { listClips, getClip, deleteClip, batchDeleteClips } from '@/api/clips'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Download } from '@element-plus/icons-vue'
import { useUserStore } from '@/stores/user'
import { useWebSocket } from '@/composables/useWebSocket'

const ws = useWebSocket()
const userStore = useUserStore()

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
const activeFormatTab = ref(0)

function formatDate(dateStr) {
  if (!dateStr) return '-'
  return new Date(dateStr).toLocaleString('zh-CN')
}

// Format type detection
function isTextFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 'text' || type === 'unicode' || type === 1 || type === 13 ||
         type === 'CF_TEXT' || type === 'CF_UNICODETEXT'
}

function isHTMLFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 'html' || type === 'CF_HTML' || type === 'CF_HTMLFORMAT' ||
         type?.includes('html')
}

function isImageFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 'image' || type === 'dib' || type === 'CF_DIB' || type === 'CF_BITMAP' ||
         type === 'CF_TIFF' || type?.includes('image')
}

function isFileFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 'file' || type === 'files' || type === 'hdop' || type === 'CF_HDROP' ||
         type?.includes('file')
}

function getFormatLabel(formatType) {
  const type = typeof formatType === 'string' ? formatType : String(formatType)
  if (isTextFormat(formatType)) return '文本'
  if (isHTMLFormat(formatType)) return 'HTML'
  if (isImageFormat(formatType)) return '图片'
  if (isFileFormat(formatType)) return '文件'
  return `格式 ${type}`
}

function getFormatName(formatType) {
  const type = typeof formatType === 'string' ? formatType : String(formatType)
  const nameMap = {
    '1': 'CF_TEXT',
    '13': 'CF_UNICODETEXT',
    'text': '文本',
    'unicode': 'Unicode 文本',
    'html': 'HTML',
    'image': '图片',
    'dib': 'DIB 图片',
    'file': '文件路径',
    'hdop': '文件 (HDROP)',
  }
  return nameMap[type] || type
}

// Data decoding helpers
function decodeTextData(fmt) {
  if (!fmt || !fmt.data) return '(无数据)'
  // If data is hex-encoded, decode it
  if (fmt.encoding === 'hex') {
    try {
      return hexToString(fmt.data)
    } catch (e) {
      return '(文本解码失败)'
    }
  }
  // Otherwise assume plain text
  return fmt.data
}

function decodeHTMLData(fmt) {
  if (!fmt || !fmt.data) return '<html><body></body></html>'
  if (fmt.encoding === 'hex') {
    try {
      return hexToString(fmt.data)
    } catch (e) {
      return '<html><body><p>HTML 解码失败</p></body></html>'
    }
  }
  return fmt.data
}

function decodeFilePaths(fmt) {
  if (!fmt || !fmt.data) return []
  try {
    // File paths might be stored as JSON array or newline-separated
    if (fmt.data.startsWith('[')) {
      return JSON.parse(fmt.data)
    }
    return fmt.data.split(/\r?\n/).filter(p => p.trim())
  } catch (e) {
    return [fmt.data]
  }
}

function hexToString(hex) {
  const str = hex.replace(/\s+/g, '')
  let result = ''
  for (let i = 0; i < str.length; i += 2) {
    const hexChar = str.substr(i, 2)
    const charCode = parseInt(hexChar, 16)
    result += String.fromCharCode(charCode)
  }
  // Try to detect UTF-8/16 and decode properly
  try {
    // For UTF-16 (Windows wide chars), every second char is null
    const hasNulls = result.includes('\0')
    if (hasNulls) {
      // Extract every other char for UTF-16LE
      let utf16Str = ''
      for (let i = 0; i < result.length; i += 2) {
        utf16Str += result[i]
      }
      return utf16Str
    }
    return result
  } catch (e) {
    return result
  }
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

// Download raw format data via backend endpoint
function handleDownloadFormat(clipId, formatType) {
  const token = userStore.token
  if (!token) {
    ElMessage.error('未登录')
    return
  }
  const url = `/api/v1/clips/${clipId}/download?format_type=${formatType}&token=${encodeURIComponent(token)}`
  // Open in hidden iframe to trigger download without leaving page
  const iframe = document.createElement('iframe')
  iframe.style.display = 'none'
  iframe.src = url
  iframe.onload = () => {
    setTimeout(() => iframe.remove(), 1000)
  }
  document.body.appendChild(iframe)
  ElMessage.success('开始下载')
}

function handleRowClick(row) {
  openClipDetail(row.id)
}

async function openClipDetail(id) {
  try {
    const res = await getClip(id)
    currentClip.value = res.data || res
    activeFormatTab.value = 0
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

.clip-detail {
  max-height: 70vh;
  overflow-y: auto;
}

.formats-section h4 {
  margin: 0 0 12px 0;
  color: #303133;
}

.format-preview {
  padding: 12px 0;
}

.format-header-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 0;
}

.format-info {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 12px;
  padding-bottom: 8px;
  border-bottom: 1px solid #ebeef5;
}

.format-name {
  font-weight: 600;
  color: #303133;
}

.format-size {
  color: #909399;
  font-size: 12px;
}

/* Text Preview */
.text-preview {
  background: #f5f7fa;
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  padding: 12px;
  max-height: 400px;
  overflow: auto;
}

.text-content {
  margin: 0;
  white-space: pre-wrap;
  word-wrap: break-word;
  font-family: 'Courier New', monospace;
  font-size: 13px;
  line-height: 1.6;
}

/* HTML Preview */
.html-preview {
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  overflow: hidden;
}

/* Image Preview */
.image-preview {
  text-align: center;
  padding: 12px;
  background: #fafafa;
  border-radius: 4px;
}

/* File Preview */
.file-preview ul {
  list-style: none;
  padding: 0;
  margin: 0;
}

.file-preview li {
  padding: 8px 12px;
  margin: 4px 0;
  background: #f5f7fa;
  border-left: 3px solid #409eff;
  font-family: 'Courier New', monospace;
  font-size: 13px;
}

/* Raw/Hex Preview */
.raw-preview {
  max-height: 300px;
  overflow: auto;
}

.hex-content {
  background: #1e1e1e;
  color: #d4d4d4;
  padding: 12px;
  border-radius: 4px;
  font-family: 'Courier New', monospace;
  font-size: 12px;
  line-height: 1.5;
  overflow-x: auto;
  margin: 0;
}
</style>
