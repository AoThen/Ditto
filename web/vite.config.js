import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath, URL } from 'node:url'

export default defineConfig({
  plugins: [vue()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  server: {
    host: '0.0.0.0',
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
      },
    },
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes('node_modules')) {
            if (id.includes('node_modules/vue') || id.includes('node_modules/vue-router') || id.includes('node_modules/pinia')) {
              return 'vendor-vue'
            }
            if (id.includes('node_modules/element-plus')) {
              return 'vendor-element'
            }
            // 其他 node_modules 按包名拆分
            const match = id.match(/node_modules[\\/](?:@[^\\/]+[\\/])?([^\\/]+)/)
            if (match) {
              return `vendor-${match[1].replace('@', '')}`
            }
          }
        },
      },
    },
  },
})
