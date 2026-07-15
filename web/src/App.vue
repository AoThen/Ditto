<script setup>
import { RouterView } from 'vue-router'
import ErrorFallback from '@/components/ErrorFallback.vue'
</script>

<template>
  <ErrorFallback>
    <RouterView />
  </ErrorFallback>
</template>
