<template>
  <div class="format-preview">
    <div v-if="loading" class="preview-loading">
      <el-icon class="is-loading"><Loading /></el-icon>
      <span>加载中...</span>
    </div>

    <div v-else-if="error" class="preview-error">
      <el-alert :title="error" type="error" :closable="false" />
    </div>

    <div v-else>
      <!-- 文本预览 -->
      <pre v-if="type === 'text'" class="preview-text">{{ content }}</pre>

      <!-- HTML 预览 -->
      <iframe
        v-else-if="type === 'html'"
        :srcdoc="content"
        class="preview-html"
        sandbox="allow-same-origin"
      />

      <!-- 图片预览 -->
      <el-image
        v-else-if="type === 'image'"
        :src="imageUrl"
        fit="contain"
        :preview-src-list="[imageUrl]"
        class="preview-image"
      />

      <!-- 文件引用 -->
      <div v-else-if="type === 'file'" class="preview-file">
        <el-alert
          title="文件引用"
          description="此剪贴板包含文件路径引用，文件内容未同步到云端"
          type="info"
          :closable="false"
        />
        <ul v-if="filePaths?.length">
          <li v-for="(path, idx) in filePaths" :key="idx">{{ path }}</li>
        </ul>
      </div>

      <!-- 未知格式 -->
      <div v-else class="preview-unknown">
        <el-alert
          :title="`未知格式: ${formatName}`"
          type="warning"
          :closable="false"
        />
        <pre>{{ rawContent }}</pre>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { Loading } from '@element-plus/icons-vue'

const props = defineProps({
  format: {
    type: Object,
    required: true
  }
})

const loading = ref(false)
const error = ref('')

const formatName = computed(() => props.format?.format_name || '')

const type = computed(() => {
  const name = formatName.value.toLowerCase()
  if (name.includes('unicode') || name.includes('text')) return 'text'
  if (name.includes('html')) return 'html'
  if (name.includes('dib') || name.includes('bitmap') || name.includes('image')) return 'image'
  if (name.includes('hdrop') || props.format?.is_file_ref) return 'file'
  return 'unknown'
})

const content = computed(() => props.format?.data || '')

const imageUrl = computed(() => {
  if (type.value === 'image' && props.format?.data_url) {
    return props.format.data_url
  }
  return ''
})

const filePaths = computed(() => {
  if (type.value === 'file' && props.format?.paths) {
    return props.format.paths
  }
  return []
})

const rawContent = computed(() => {
  try {
    return JSON.stringify(props.format, null, 2)
  } catch {
    return String(props.format)
  }
})

onMounted(() => {
  loading.value = false
})
</script>

<style scoped>
.format-preview {
  min-height: 100px;
}

.preview-text {
  background: #f5f5f5;
  padding: 12px;
  border-radius: 4px;
  max-height: 300px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-all;
  font-size: 13px;
}

.preview-html {
  width: 100%;
  height: 300px;
  border: 1px solid #eee;
  border-radius: 4px;
}

.preview-image {
  max-width: 100%;
  max-height: 400px;
}

.preview-file ul {
  margin: 8px 0;
  padding-left: 20px;
}

.preview-file li {
  font-family: monospace;
  font-size: 12px;
  color: #666;
}

.preview-loading,
.preview-error {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 20px;
  color: #999;
}

.preview-unknown pre {
  background: #f5f5f5;
  padding: 12px;
  border-radius: 4px;
  max-height: 200px;
  overflow: auto;
  font-size: 12px;
}
</style>
