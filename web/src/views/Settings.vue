<template>
  <div class="settings-container">
    <el-card>
      <template #header>
        <h2>设置</h2>
      </template>

      <!-- Encryption Section -->
      <div class="settings-section">
        <h3>端到端加密</h3>
        <div class="setting-item">
          <span class="label">加密状态</span>
          <el-tag :type="encryptionEnabled ? 'success' : 'info'">
            {{ encryptionEnabled ? '已启用' : '未启用' }}
          </el-tag>
        </div>

        <div class="setting-item">
          <span class="label">加密盐值</span>
          <div class="salt-display">
            <span>{{ showSalt ? saltValue : maskedSalt }}</span>
            <el-button link type="primary" @click="showSalt = !showSalt">
              {{ showSalt ? '隐藏' : '显示' }}
            </el-button>
          </div>
        </div>

        <div class="setting-item">
          <span class="label">密码提示</span>
          <el-input
            v-model="passwordHint"
            placeholder="输入密码提示（可选）"
            style="width: 300px"
          />
        </div>

        <div class="setting-item">
          <el-button type="primary" :loading="setupLoading" @click="handleEnableEncryption">
            启用加密
          </el-button>
        </div>
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { getEncryptionSalt, setupEncryption } from '@/api/clips'
import { ElMessage } from 'element-plus'

const encryptionEnabled = ref(false)
const saltValue = ref('')
const showSalt = ref(false)
const passwordHint = ref('')
const setupLoading = ref(false)

const maskedSalt = '****'

async function fetchSalt() {
  try {
    const res = await getEncryptionSalt()
    if (res.code === 200) {
      saltValue.value = res.data?.salt || ''
      encryptionEnabled.value = !!res.data?.salt
    }
  } catch (err) {
    console.error('Failed to fetch encryption salt:', err)
    // Salt endpoint may not exist yet; that's okay
  }
}

async function handleEnableEncryption() {
  if (!passwordHint.value) {
    ElMessage.warning('请输入密码提示')
    return
  }
  setupLoading.value = true
  try {
    await setupEncryption({
      password_hint: passwordHint.value,
    })
    ElMessage.success('加密设置成功')
    encryptionEnabled.value = true
    fetchSalt()
  } catch (err) {
    console.error('Failed to setup encryption:', err)
    ElMessage.error('启用加密失败')
  } finally {
    setupLoading.value = false
  }
}

onMounted(() => {
  fetchSalt()
})
</script>

<style scoped>
.settings-container {
  padding: 20px;
}

.settings-section {
  padding: 16px 0;
}

.settings-section h3 {
  margin: 0 0 16px;
  font-size: 18px;
  color: #303133;
}

.setting-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 0;
  border-bottom: 1px solid #ebeef5;
}

.setting-item:last-child {
  border-bottom: none;
}

.setting-item .label {
  min-width: 100px;
  font-weight: 500;
  color: #606266;
}

.salt-display {
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: monospace;
  font-size: 14px;
}
</style>
