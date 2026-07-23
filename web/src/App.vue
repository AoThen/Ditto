<script setup>
import { RouterView } from 'vue-router'
import ErrorFallback from '@/components/ErrorFallback.vue'
import zhCn from 'element-plus/dist/locale/zh-cn.mjs'
</script>

<template>
  <el-config-provider :locale="zhCn">
    <ErrorFallback>
      <RouterView />
    </ErrorFallback>
  </el-config-provider>
</template>
