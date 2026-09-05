<template>
  <div class="clips-container">
    <div class="toolbar">
      <div class="ws-status" role="status" aria-live="polite" :class="{ connected: ws.isConnected, disconnected: !ws.isConnected }">
        {{ ws.isConnected ? '● 实时同步中' : '○ 未连接' }}
      </div>
      <el-input
        v-model="searchQuery"
        placeholder="搜索剪贴板内容...（支持拼音搜索）"
        clearable
        style="width: 300px"
        @clear="handleSearch"
        @input="handleSearchDebounced"
        @keyup.enter="handleSearch"
      >
        <template #append>
          <el-button @click="handleSearch">搜索</el-button>
        </template>
      </el-input>
      <el-select v-model="groupFilter" placeholder="全部分组" clearable style="width: 150px" @change="handleGroupFilterChange">
        <el-option label="全部分组" value="" />
        <el-option v-for="g in groups" :key="g.id" :label="g.name" :value="g.id" />
      </el-select>
      <el-button type="primary" @click="handleRefresh">刷新</el-button>
      <el-button type="warning" @click="showConflictDialog">
        冲突剪贴板 ({{ conflictCount }})
      </el-button>
      <el-dropdown v-if="selectedRows.length" @command="handleGroupAction" style="margin-left: 4px;">
        <el-button type="info">
          分组 <el-icon><ArrowDown /></el-icon>
        </el-button>
        <template #dropdown>
          <el-dropdown-menu>
            <el-dropdown-item command="move-to-group">移动到分组</el-dropdown-item>
            <el-dropdown-item command="remove-from-group" :disabled="!allSelectedInGroup">从分组移除</el-dropdown-item>
          </el-dropdown-menu>
        </template>
      </el-dropdown>
      <el-button type="danger" :disabled="!selectedRows.length || batchDeleting" :loading="batchDeleting" @click="handleBatchDelete">
        删除选中 ({{ selectedRows.length }})
      </el-button>
      <el-button type="warning" :disabled="!selectedRows.length" @click="handleBatchMarkDontSync">
        标记不同步
      </el-button>
    </div>

    <el-table
      :data="clipList"
      v-loading="loading"
      @selection-change="handleSelectionChange"
      @row-click="handleRowClick"
      @sort-change="handleSortChange"
      style="width: 100%; margin-top: 16px"
    >
      <el-table-column type="selection" width="55" />
      <el-table-column prop="description" label="描述" show-overflow-tooltip sortable="custom" />
      <el-table-column prop="paste_count" label="粘贴次数" width="100" sortable="custom" />
      <el-table-column prop="group_name" label="分组" width="120">
        <template #default="{ row }">
          <el-tag v-if="row.group_name" size="small">{{ row.group_name }}</el-tag>
          <span v-else class="no-group">-</span>
        </template>
      </el-table-column>
      <el-table-column prop="created_at" label="创建时间" width="180" sortable="custom">
        <template #default="{ row }">
          {{ formatDate(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="120">
        <template #default="{ row }">
          <el-button
            type="danger"
            size="small"
            :loading="deletingId === row.id"
            :disabled="deletingId !== null || batchDeleting"
            @click.stop="handleDelete(row.id)"
          >
            删除
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-empty v-if="clipList.length === 0 && !loading" description="暂无剪贴板记录，复制内容后将在此处显示" />

    <el-pagination
      v-model:current-page="currentPage"
      :page-size="20"
      :total="total"
      layout="total, prev, pager, next"
      aria-label="剪贴板分页导航"
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

        <div style="margin-top: 12px; text-align: right;">
          <el-button
            type="primary"
            size="small"
            @click="copyToClipboard"
            :disabled="!hasTextFormat"
          >
            <el-icon><CopyDocument /></el-icon> 复制到剪贴板
          </el-button>
        </div>

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
                    sandbox=""
                    title="HTML 剪贴板预览"
                    style="width: 100%; height: 400px; border: 1px solid #dcdfe6; border-radius: 4px;"
                  ></iframe>
                </div>

                <!-- Image Preview -->
                <div v-else-if="isImageFormat(fmt.format_type)" class="image-preview">
                  <el-image
                    v-if="fmt.data"
                    :src="'data:' + getImageMime(fmt.format_type) + ';base64,' + fmt.data"
                    fit="contain"
                    style="max-height: 400px;"
                    :preview-src-list="['data:' + getImageMime(fmt.format_type) + ';base64,' + fmt.data]"
                  />
                  <el-alert v-if="!fmt.data" title="图片数据不可预览" type="info" :closable="false" />
                  <el-alert
                    v-if="fmt.data && (fmt.format_type === 8 || fmt.format_type === 17)"
                    title="此图片为 BMP 格式，部分浏览器可能无法直接预览"
                    type="warning"
                    :closable="false"
                    style="margin-top: 8px"
                  />
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

    <!-- Move to Group Dialog -->
    <el-dialog v-model="groupDialogVisible" title="移动到分组" width="400px">
      <el-form>
        <el-form-item label="目标分组">
          <el-select v-model="selectedGroupId" placeholder="选择分组" style="width: 100%" filterable>
            <el-option
              v-for="g in groupList"
              :key="g.id"
              :label="g.name"
              :value="g.id"
            >
              <span>{{ g.name }}</span>
              <span class="group-clip-count">({{ g.clip_count || 0 }} 项)</span>
            </el-option>
          </el-select>
        </el-form-item>
      </el-form>
      <el-empty v-if="groupList.length === 0" description="暂无分组，请先在分组页面创建" />
      <template #footer>
        <el-button @click="groupDialogVisible = false">取消</el-button>
        <el-button type="primary" :disabled="!selectedGroupId || groupMoving" :loading="groupMoving" @click="handleMoveToGroup">
          确定
        </el-button>
      </template>
    </el-dialog>

    <!-- Conflict Clips Dialog -->
    <el-dialog v-model="conflictDialogVisible" title="冲突剪贴板" width="800px" @opened="fetchConflictClips">
      <el-alert
        title="以下剪贴板在同步时因内容冲突被保留（较旧版本），您可以选择接受（覆盖现有剪贴板）或丢弃（删除冲突副本）。"
        type="warning"
        :closable="true"
        style="margin-bottom: 16px"
      />
      <el-table :data="conflictClips" v-loading="conflictLoading" style="width: 100%">
        <el-table-column prop="description" label="描述" show-overflow-tooltip />
        <el-table-column prop="crc" label="CRC" width="120" />
        <el-table-column prop="updated_at" label="冲突时间" width="180">
          <template #default="{ row }">
            {{ formatDate(row.updated_at) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="180">
          <template #default="{ row }">
            <el-button
              size="small"
              type="primary"
              :loading="resolvingId === row.id"
              :disabled="resolvingId !== null"
              :aria-label="`接受剪贴板 ${row.description || row.id}`"
              @click="handleResolveConflict(row, 'accept')"
            >接受</el-button>
            <el-button
              size="small"
              type="danger"
              :loading="resolvingId === row.id"
              :disabled="resolvingId !== null"
              :aria-label="`丢弃剪贴板 ${row.description || row.id}`"
              @click="handleResolveConflict(row, 'discard')"
            >丢弃</el-button>
          </template>
        </el-table-column>
      </el-table>
      <el-pagination
        v-if="conflictTotal > 20"
        v-model:current-page="conflictPage"
        :page-size="20"
        :total="conflictTotal"
        layout="total, prev, pager, next"
        style="margin-top: 16px; justify-content: center"
        @current-change="fetchConflictClips"
      />
      <el-empty v-if="conflictClips.length === 0 && !conflictLoading" description="暂无冲突剪贴板" />
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { listClips, getClip, deleteClip, getChanges, batchDeleteClips, batchMarkDontSync } from '@/api/clips'
import { listConflictClips, resolveConflictClip } from '@/api/conflicts'
import { listGroups, moveClipsToGroup, removeClipsFromGroup } from '@/api/groups'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Download, ArrowDown, CopyDocument } from '@element-plus/icons-vue'
import { useWebSocket } from '@/composables/useWebSocket'
import { downloadBlob } from '@/api/request'
import { formatDate } from '@/composables/useFormatDate'
import { useClipStore } from '@/stores/clip'

const ws = useWebSocket()
const clipStore = useClipStore()

// Listen for WS clip notifications
function onWsClipAdded(event) {
  console.log('[Clips] WS clip added:', event.detail)
  if (searchQuery.value) {
    fetchClips()
    return
  }
  incrementalSync(event.detail?.device_id)
}

// P0-B FIX: React to deletion broadcasts so the list stays in sync when another
// device deletes clips. (Own-device deletions are filtered in useWebSocket and
// already handled locally by handleDelete/handleBatchDelete.)
function onWsClipsDeleted(event) {
  const clipIds = event.detail || []
  if (!Array.isArray(clipIds) || clipIds.length === 0) return
  const idSet = new Set(clipIds.map(String))
  const before = clipList.value.length
  clipList.value = clipList.value.filter((c) => !idSet.has(String(c.id)))
  const removed = before - clipList.value.length
  if (removed > 0) {
    total.value = Math.max(0, total.value - removed)
    ElMessage.info(`有 ${removed} 条剪贴板被其他设备删除`)
  }
}

// Safety valve so a misbehaving server can never loop the sync forever.
const SYNC_MAX_PAGES = 10
const SYNC_PAGE_SIZE = 1000

async function incrementalSync(deviceId) {
  try {
    // P1-A FIX: share a single persistent sync cursor with useWebSocket.
    const since = clipStore.lastSyncTime || '1970-01-01T00:00:00Z'

    // Drain every page: one response is capped by the server, and the watermark
    // must only advance once the whole backlog was consumed — otherwise the
    // unfetched tail would be skipped forever.
    let changed = false
    let serverTime = ''
    let drained = false
    // Deletions are not paged server-side: every response repeats the whole
    // list, so track them across pages to avoid subtracting the same id twice.
    const seenDeleted = new Set()
    for (let page = 1; page <= SYNC_MAX_PAGES; page++) {
      const res = await getChanges(since, page, SYNC_PAGE_SIZE)
      if (res.code !== 0) break
      const { clips, deleted_ids, server_time } = res.data
      if (server_time) serverTime = server_time

      const freshDeleted = (deleted_ids || []).filter(id => !seenDeleted.has(id))
      freshDeleted.forEach(id => seenDeleted.add(id))
      if (freshDeleted.length > 0) {
        clipList.value = clipList.value.filter(c => !seenDeleted.has(c.id))
        total.value = Math.max(0, total.value - freshDeleted.length)
        changed = true
      }
      if (clips?.length > 0) {
        for (const clip of clips) {
          const idx = clipList.value.findIndex(c => c.id === clip.id)
          if (idx >= 0) {
            clipList.value[idx] = { ...clipList.value[idx], ...clip }
          } else {
            clipList.value.unshift(clip)
            total.value++
          }
        }
        changed = true
      }
      if (!res.data.has_more) {
        drained = true
        break
      }
    }
    // Only a fully drained backlog may move the cursor: the server timestamp is
    // newer than the clips still sitting on the unfetched pages.
    if (drained && serverTime) clipStore.updateSyncTime(serverTime)

    if (changed && deviceId) {
      ElMessage.success(`收到来自 ${deviceId} 的新剪贴板`)
    }
  } catch (err) {
    console.error('Incremental sync failed, falling back to full refresh:', err)
    fetchClips()
  }
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
const deletingId = ref(null)
const batchDeleting = ref(false)

// Sort state
const sortBy = ref('')
const sortOrder = ref('')

// Conflict clips state
const conflictClips = ref([])
const conflictLoading = ref(false)
const conflictDialogVisible = ref(false)
const conflictPage = ref(1)
const conflictTotal = ref(0)
// The badge must show the total number of conflicts, not how many happen to be
// on the currently loaded page.
const conflictCount = ref(0)
const resolvingId = ref(null)

// Group state
const groupDialogVisible = ref(false)
const groupList = ref([])
const selectedGroupId = ref(null)
const groupMoving = ref(false)

// Group filter
const groupFilter = ref('')
const groups = ref([])

// Clipboard detail helpers
const hasTextFormat = computed(() => {
  if (!currentClip.value?.formats) return false
  return currentClip.value.formats.some(f => isTextFormat(f.format_type))
})

async function copyToClipboard() {
  if (!currentClip.value?.formats) return
  const textFormat = currentClip.value.formats.find(f => isTextFormat(f.format_type))
  if (!textFormat) return
  const text = decodeTextData(textFormat)
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success('已复制到剪贴板')
  } catch {
    const ta = document.createElement('textarea')
    ta.value = text
    ta.style.position = 'fixed'
    ta.style.opacity = '0'
    document.body.appendChild(ta)
    ta.select()
    document.execCommand('copy')
    document.body.removeChild(ta)
    ElMessage.success('已复制到剪贴板')
  }
}

// Search debounce timer
let searchTimer = null

// Format type detection
function getImageMime(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  if (type === 50 || type === '50') return 'image/png'
  if (type === 8 || type === 17 || type === '8' || type === '17') return 'image/bmp'
  if (type === 'image' || type === 'dib' || type === 'CF_DIB' || type === 'CF_BITMAP') return 'image/bmp'
  return 'image/png'
}

function isTextFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 1 || type === 13 || type === 7 ||
         type === 'text' || type === 'unicode' || type === 'CF_TEXT' || type === 'CF_UNICODETEXT'
}

function isHTMLFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 49 ||
         type === 'html' || type === 'CF_HTML' || type === 'CF_HTMLFORMAT' ||
         type?.includes('html')
}

function isImageFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 8 || type === 17 || type === 50 ||
         type === 'image' || type === 'dib' || type === 'CF_DIB' || type === 'CF_BITMAP' ||
         type?.includes('image')
}

function isFileFormat(formatType) {
  const type = typeof formatType === 'string' ? formatType.toLowerCase() : formatType
  return type === 15 ||
         type === 'file' || type === 'files' || type === 'hdop' || type === 'CF_HDROP' ||
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
import { base64ToUtf8 } from '@/utils/base64'

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
  // Text formats are stored as base64(UTF-8 bytes) by the backend.
  // Without this decode, Chinese/emoji text previews and copies show base64 garbage.
  if (isTextFormat(fmt.format_type)) {
    const decoded = base64ToUtf8(fmt.data)
    if (decoded !== null) return decoded
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
  // HTML format data is also base64(UTF-8 bytes)
  if (isHTMLFormat(fmt.format_type)) {
    const decoded = base64ToUtf8(fmt.data)
    if (decoded !== null) return decoded
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
    if (groupFilter.value) {
      params.group_id = groupFilter.value
    }
    if (sortBy.value) {
      params.sort_by = sortBy.value
      params.sort_order = sortOrder.value
    }
    const res = await listClips(params)
    // Backend returns code: 0 for success (not 200)
    clipList.value = res.data?.items || res.data || []
    total.value = res.data?.total || 0
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

function handleGroupFilterChange() {
  currentPage.value = 1
  fetchClips()
}

function handleSearchDebounced() {
  if (searchTimer) clearTimeout(searchTimer)
  searchTimer = setTimeout(() => {
    handleSearch()
  }, 300)
}

function handleSortChange({ prop, order }) {
  sortBy.value = prop || ''
  sortOrder.value = order === 'ascending' ? 'asc' : order === 'descending' ? 'desc' : ''
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

// Download raw format data via axios + Blob (auth via HttpOnly cookies - H1)
async function handleDownloadFormat(clipId, formatType) {
  try {
    const blob = await downloadBlob(`/api/v1/clips/${clipId}/download`, {
      params: { format_type: formatType }
    })
    // Create Blob and trigger download
    const url = window.URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `clip_${clipId}_${formatType}`
    document.body.appendChild(a)
    a.click()
    a.remove()
    window.URL.revokeObjectURL(url)
    ElMessage.success('下载完成')
  } catch (err) {
    console.error('Failed to download format:', err)
    ElMessage.error('下载失败')
  }
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
    deletingId.value = id
    await deleteClip(id)
    ElMessage.success('删除成功')
    // Use loose comparison to handle string vs number ID mismatch
    clipList.value = clipList.filter((c) => c.id != id)
    total.value = Math.max(0, total.value - 1)
  } catch (err) {
    if (err !== 'cancel') {
      console.error('Failed to delete clip:', err)
      ElMessage.error('删除剪贴板失败')
    }
  } finally {
    deletingId.value = null
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
    batchDeleting.value = true
    const ids = selectedRows.value.map(row => row.id)
    const res = await batchDeleteClips(ids)
    ElMessage.success(`成功删除 ${res.data?.deleted || ids.length} 个剪贴板`)
    selectedRows.value = []
    fetchClips()
  } catch (err) {
    if (err !== 'cancel') {
      console.error('Failed to batch delete clips:', err)
      ElMessage.error('批量删除剪贴板失败')
    }
  } finally {
    batchDeleting.value = false
  }
}

async function handleBatchMarkDontSync() {
  if (!selectedRows.value.length) return
  try {
    await ElMessageBox.confirm(
      `确定将选中的 ${selectedRows.value.length} 个剪贴板标记为不同步？`,
      '确认标记',
      { confirmButtonText: '确定', cancelButtonText: '取消', type: 'warning' }
    )
    const ids = selectedRows.value.map(row => row.id)
    await batchMarkDontSync(ids)
    ElMessage.success('标记成功')
    selectedRows.value = []
    fetchClips()
  } catch (err) {
    if (err !== 'cancel') {
      ElMessage.error('标记失败: ' + (err.message || '未知错误'))
    }
  }
}

// Conflict clip functions
async function showConflictDialog() {
  conflictDialogVisible.value = true
}

async function fetchConflictClips() {
  conflictLoading.value = true
  try {
    const res = await listConflictClips(conflictPage.value, 20)
    if (res.code === 0) {
      conflictClips.value = res.data?.items || res.data || []
      conflictTotal.value = res.data?.total || 0
      conflictCount.value = conflictTotal.value
    }
  } catch (err) {
    ElMessage.error('获取冲突剪贴板失败: ' + err.message)
  } finally {
    conflictLoading.value = false
  }
}

async function handleResolveConflict(clip, action) {
  const actionLabel = action === 'accept' ? '接受' : '丢弃'
  try {
    await ElMessageBox.confirm(
      action === 'accept'
        ? `确定接受此冲突剪贴板吗？这将覆盖现有剪贴板的内容。`
        : `确定丢弃此冲突剪贴板吗？冲突副本将被删除。`,
      `确认${actionLabel}`,
      { confirmButtonText: '确定', cancelButtonText: '取消', type: 'warning' }
    )

    resolvingId.value = clip.id
    const res = await resolveConflictClip(clip.id, action)
    if (res.code === 0) {
      ElMessage.success(`${actionLabel}成功`)
      await fetchConflictClips()
      // Refresh main clip list if accepted
      if (action === 'accept') {
        await fetchClips()
      }
    }
  } catch (err) {
    if (err !== 'cancel') {
      // Check if 404 (already resolved)
      const errMsg = err.message || ''
      if (errMsg.includes('不存在') || errMsg.includes('not found') || errMsg.includes('404')) {
        ElMessage.info('该冲突剪贴板已不存在')
        await fetchConflictClips()
      } else {
        ElMessage.error(`${actionLabel}失败: ${errMsg}`)
      }
    }
  } finally {
    resolvingId.value = null
  }
}

// Group operations
const allSelectedInGroup = computed(() =>
  selectedRows.value.length > 0 && selectedRows.value.every(r => r.group_name)
)

async function handleGroupAction(command) {
  if (command === 'move-to-group') {
    await openGroupDialog()
  } else if (command === 'remove-from-group') {
    await handleRemoveFromGroup()
  }
}

async function openGroupDialog() {
  try {
    const res = await listGroups()
    if (res.code === 0) {
      groupList.value = res.data?.items || res.data || []
    }
  } catch (err) {
    ElMessage.error('获取分组列表失败')
  }
  selectedGroupId.value = null
  groupDialogVisible.value = true
}

async function handleMoveToGroup() {
  if (!selectedGroupId.value || !selectedRows.value.length) return
  groupMoving.value = true
  try {
    const clipIds = selectedRows.value.map(r => r.id)
    await moveClipsToGroup(selectedGroupId.value, clipIds)
    ElMessage.success('移动到分组成功')
    groupDialogVisible.value = false
    selectedRows.value = []
    fetchClips()
  } catch (err) {
    ElMessage.error('移动到分组失败: ' + (err.message || ''))
  } finally {
    groupMoving.value = false
  }
}

async function handleRemoveFromGroup() {
  if (!selectedRows.value.length) return
  try {
    await ElMessageBox.confirm('确定将选中剪贴板从当前分组移除？', '确认', {
      confirmButtonText: '确定', cancelButtonText: '取消', type: 'warning',
    })
    const clipIds = selectedRows.value.map(r => r.id)
    await removeClipsFromGroup(clipIds)
    ElMessage.success('已从分组移除')
    selectedRows.value = []
    fetchClips()
  } catch (err) {
    if (err !== 'cancel') {
      ElMessage.error('移除分组失败: ' + (err.message || ''))
    }
  }
}

async function fetchGroups() {
  try {
    const res = await listGroups()
    if (res.code === 0) {
      groups.value = res.data?.items || res.data || []
    }
  } catch (err) {
    console.error('Failed to fetch groups:', err)
  }
}

onMounted(async () => {
  window.addEventListener('ws-clip-added', onWsClipAdded)
  window.addEventListener('ws-clips-deleted', onWsClipsDeleted)
  await Promise.all([fetchClips(), fetchConflictClips()])
  // P1-A FIX: advance the shared cursor after the full list is loaded so a
  // subsequent WS-triggered incremental sync does not re-fetch everything.
  clipStore.updateSyncTime(new Date().toISOString())
  fetchGroups()
})

onUnmounted(() => {
  // MEDIUM FIX (M4): Clear search debounce timer to prevent stale callbacks
  if (searchTimer) {
    clearTimeout(searchTimer)
    searchTimer = null
  }
  window.removeEventListener('ws-clip-added', onWsClipAdded)
  window.removeEventListener('ws-clips-deleted', onWsClipsDeleted)
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

.no-group {
  color: #c0c4cc;
}

.group-clip-count {
  color: #909399;
  font-size: 12px;
  margin-left: 8px;
}
</style>
