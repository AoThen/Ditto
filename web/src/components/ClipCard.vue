<template>
  <el-card class="clip-card" shadow="hover" @click="$emit('click')">
    <div class="clip-card__header">
      <el-tag v-if="clip.is_group" type="warning" size="small">分组</el-tag>
      <el-tag v-else type="info" size="small">{{ formatType }}</el-tag>
      <span class="clip-card__date">{{ formatDate(clip.created_at) }}</span>
    </div>
    <div class="clip-card__body">
      <p class="clip-card__text">{{ clip.description || '无描述' }}</p>
    </div>
    <div class="clip-card__footer">
      <el-button size="small" @click.stop="$emit('copy', clip)">复制</el-button>
      <el-button size="small" type="danger" @click.stop="$emit('delete', clip)">删除</el-button>
    </div>
  </el-card>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  clip: {
    type: Object,
    required: true
  }
})

defineEmits(['click', 'copy', 'delete'])

const formatType = computed(() => {
  if (!props.clip.formats || props.clip.formats.length === 0) return '未知'
  return props.clip.formats[0].format_name || '文本'
})

const formatDate = (dateStr) => {
  if (!dateStr) return ''
  const date = new Date(dateStr)
  return date.toLocaleString('zh-CN')
}
</script>

<style scoped>
.clip-card {
  cursor: pointer;
  transition: transform 0.2s;
}

.clip-card:hover {
  transform: translateY(-2px);
}

.clip-card__header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.clip-card__date {
  font-size: 12px;
  color: #999;
}

.clip-card__body {
  margin-bottom: 12px;
}

.clip-card__text {
  margin: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 14px;
  color: #333;
}

.clip-card__footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>
