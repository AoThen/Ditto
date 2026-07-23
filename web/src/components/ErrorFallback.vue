<template>
  <div class="error-fallback" v-if="hasError">
    <el-result
      icon="error"
      title="页面渲染异常"
      sub-title="发生了一个意外错误，请尝试刷新页面"
    >
      <template #extra>
        <el-button type="primary" @click="handleRetry">重新加载</el-button>
      </template>
    </el-result>
  </div>
  <slot v-else />
</template>

<script setup>
import { ref, onErrorCaptured } from 'vue'

const hasError = ref(false)

onErrorCaptured(() => {
  hasError.value = true
  return false
})

function handleRetry() {
  hasError.value = false
  window.location.reload()
}
</script>

<style scoped>
.error-fallback {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 60vh;
}
</style>