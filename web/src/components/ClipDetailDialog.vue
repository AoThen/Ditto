<template>
  <el-dialog
    v-model="visible"
    title="剪贴板详情"
    width="700px"
    :before-close="handleClose"
  >
    <el-tabs v-if="clip" v-model="activeTab">
      <el-tab-pane label="文本" name="text">
        <pre class="detail-content">{{ textContent }}</pre>
      </el-tab-pane>
      <el-tab-pane label="HTML" name="html">
        <div v-html="htmlContent" class="detail-html"></div>
      </el-tab-pane>
      <el-tab-pane label="图片" name="image">
        <el-image
          v-if="imageUrl"
          :src="imageUrl"
          fit="contain"
          style="max-width: 100%"
        />
        <span v-else>无图片</span>
      </el-tab-pane>
      <el-tab-pane label="原始数据" name="raw">
        <pre class="detail-content">{{ rawContent }}</pre>
      </el-tab-pane>
    </el-tabs>

    <template #footer>
      <el-button @click="handleClose">关闭</el-button>
      <el-button type="primary" @click="handleCopy">复制到剪贴板</el-button>
    </template>
  </el-dialog>
</template>

<script setup>
import { ref, computed, watch } from 'vue'

const props = defineProps({
  modelValue: {
    type: Boolean,
    default: false
  },
  clip: {
    type: Object,
    default: null
  }
})

const emit = defineEmits(['update:modelValue', 'copy'])

const visible = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val)
})

const activeTab = ref('text')

const textContent = computed(() => {
  if (!props.clip?.formats) return ''
  const textFormat = props.clip.formats.find(f => f.format_name === 'CF_UNICODETEXT' || f.format_name === 'CF_TEXT')
  return textFormat?.data || ''
})

const htmlContent = computed(() => {
  if (!props.clip?.formats) return ''
  const htmlFormat = props.clip.formats.find(f => f.format_name?.includes('HTML'))
  return htmlFormat?.data || ''
})

const imageUrl = computed(() => {
  if (!props.clip?.formats) return ''
  const imgFormat = props.clip.formats.find(f => f.format_name === 'CF_DIB')
  return imgFormat?.data_url || ''
})

const rawContent = computed(() => {
  if (!props.clip) return ''
  return JSON.stringify(props.clip, null, 2)
})

const handleClose = () => {
  visible.value = false
}

const handleCopy = () => {
  emit('copy', props.clip)
}
</script>

<style scoped>
.detail-content {
  background: #f5f5f5;
  padding: 12px;
  border-radius: 4px;
  max-height: 400px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-all;
}

.detail-html {
  max-height: 400px;
  overflow: auto;
}
</style>
