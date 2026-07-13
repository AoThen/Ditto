<template>
  <div class="settings-container">
    <!-- User Profile Section -->
    <el-card style="margin-bottom: 20px;">
      <template #header>
        <h2>账户信息</h2>
      </template>
      <div class="settings-section">
        <div class="setting-item">
          <span class="label">用户名</span>
          <span>{{ userStore.username || '(未设置)' }}</span>
        </div>
        <div class="setting-item">
          <span class="label">设备 ID</span>
          <span class="monospace">{{ userStore.deviceId || '(未登录)' }}</span>
        </div>
        <div class="setting-item">
          <el-button type="danger" plain @click="handleLogout">退出登录</el-button>
        </div>
      </div>
    </el-card>

    <!-- Encryption Section -->
    <el-card>
      <template #header>
        <h2>端到端加密</h2>
      </template>
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

        <!-- Encryption Password Input -->
        <div v-if="!encryptionEnabled" class="setting-item">
          <span class="label">加密密码</span>
          <el-input
            v-model="encryptionPassword"
            type="password"
            placeholder="设置加密密码（遗忘后数据不可恢复）"
            show-password
            style="width: 300px"
            @keyup.enter="handleEnableEncryption"
          />
        </div>

        <div v-if="!encryptionEnabled" class="setting-item">
          <span class="label">确认密码</span>
          <el-input
            v-model="confirmPassword"
            type="password"
            placeholder="再次输入加密密码"
            show-password
            style="width: 300px"
            @keyup.enter="handleEnableEncryption"
          />
        </div>

        <div v-if="encryptionEnabled" class="setting-item">
          <span class="label">修改密码</span>
          <el-button type="warning" @click="showChangePasswordDialog">修改加密密码</el-button>
        </div>

        <div v-if="encryptionEnabled" class="setting-item">
          <span class="label">禁用加密</span>
          <el-button type="danger" @click="handleDisableEncryption">禁用端到端加密</el-button>
        </div>

        <div v-if="!encryptionEnabled" class="setting-item">
          <span class="label">密码提示</span>
          <el-input
            v-model="passwordHint"
            placeholder="输入密码提示（可选）"
            style="width: 300px"
          />
        </div>

        <div v-if="!encryptionEnabled" class="setting-item">
          <el-alert
            title="注意：加密密码遗忘后，云端数据将无法解密。请妥善保管密码或导出密钥文件备份。"
            type="warning"
            :closable="false"
            style="width: 300px"
          />
        </div>

        <div v-if="!encryptionEnabled" class="setting-item">
          <el-button type="primary" :loading="setupLoading" @click="handleEnableEncryption">
            启用加密
          </el-button>
        </div>
      </div>
    </el-card>

    <!-- Change Password Dialog -->
    <el-dialog v-model="changePasswordDialogVisible" title="修改加密密码" width="400px">
      <el-form :model="changePasswordForm" label-width="80px">
        <el-form-item label="旧密码">
          <el-input v-model="changePasswordForm.oldPassword" type="password" show-password />
        </el-form-item>
        <el-form-item label="新密码">
          <el-input v-model="changePasswordForm.newPassword" type="password" show-password />
        </el-form-item>
        <el-form-item label="确认新密码">
          <el-input v-model="changePasswordForm.confirmPassword" type="password" show-password />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="changePasswordDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="changePasswordLoading" @click="handleChangePassword">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { getEncryptionSalt, setupEncryption, disableEncryption, changeEncryptionPassword } from '@/api/clips'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useUserStore } from '@/stores/user'

const router = useRouter()
const userStore = useUserStore()
const encryptionEnabled = ref(false)
const saltValue = ref('')
const showSalt = ref(false)
const encryptionPassword = ref('')
const confirmPassword = ref('')
const passwordHint = ref('')
const setupLoading = ref(false)

const changePasswordDialogVisible = ref(false)
const changePasswordForm = ref({
  oldPassword: '',
  newPassword: '',
  confirmPassword: '',
})
const changePasswordLoading = ref(false)

const maskedSalt = '****'

async function fetchSalt() {
  try {
    const res = await getEncryptionSalt()
    if (res.code === 0) {
      saltValue.value = res.data?.salt || ''
      encryptionEnabled.value = !!res.data?.salt
    }
  } catch (err) {
    console.error('Failed to fetch encryption salt:', err)
    // Salt endpoint may not exist yet; that's okay
  }
}

async function handleEnableEncryption() {
  if (!encryptionPassword.value) {
    ElMessage.warning('请输入加密密码')
    return
  }
  if (!confirmPassword.value) {
    ElMessage.warning('请再次输入加密密码')
    return
  }
  if (encryptionPassword.value !== confirmPassword.value) {
    ElMessage.error('两次输入的密码不一致')
    return
  }
  if (encryptionPassword.value.length < 8) {
    ElMessage.warning('加密密码至少需要 8 位')
    return
  }
  setupLoading.value = true
  try {
    await setupEncryption({
      password: encryptionPassword.value,
      password_hint: passwordHint.value,
    })
    ElMessage.success('加密设置成功')
    encryptionEnabled.value = true
    encryptionPassword.value = ''
    confirmPassword.value = ''
    fetchSalt()
  } catch (err) {
    console.error('Failed to setup encryption:', err)
    ElMessage.error('启用加密失败')
  } finally {
    setupLoading.value = false
  }
}

async function handleDisableEncryption() {
  try {
    await ElMessageBox.confirm(
      '禁用加密后，云端将存储明文数据。确定要禁用端到端加密吗？',
      '警告',
      { confirmButtonText: '确定', cancelButtonText: '取消', type: 'warning' }
    )
    await disableEncryption()
    ElMessage.success('已禁用端到端加密')
    encryptionEnabled.value = false
    fetchSalt()
  } catch (err) {
    if (err !== 'cancel') {
      console.error('Failed to disable encryption:', err)
      ElMessage.error('禁用加密失败')
    }
  }
}

function showChangePasswordDialog() {
  changePasswordDialogVisible.value = true
  changePasswordForm.value = {
    oldPassword: '',
    newPassword: '',
    confirmPassword: '',
  }
}

async function handleChangePassword() {
  const { oldPassword, newPassword, confirmPassword: confirm } = changePasswordForm.value
  if (!oldPassword) {
    ElMessage.warning('请输入旧密码')
    return
  }
  if (!newPassword) {
    ElMessage.warning('请输入新密码')
    return
  }
  if (newPassword !== confirm) {
    ElMessage.error('两次输入的新密码不一致')
    return
  }
  if (newPassword.length < 8) {
    ElMessage.warning('新密码至少需要 8 位')
    return
  }
  changePasswordLoading.value = true
  try {
    // Update the password hint on the server
    await changeEncryptionPassword(newPassword)
    ElMessage.success('密码提示已更新，请使用新密码重新加密数据')
    changePasswordDialogVisible.value = false
  } catch (err) {
    console.error('Failed to change encryption password:', err)
    ElMessage.error('修改密码提示失败')
  } finally {
    changePasswordLoading.value = false
  }
}

async function handleLogout() {
  try {
    await ElMessageBox.confirm('确定退出登录吗？', '确认', {
      confirmButtonText: '确定', cancelButtonText: '取消', type: 'info',
    })
    userStore.logout()
    router.push('/login')
    ElMessage.success('已退出登录')
  } catch (err) {
    // cancelled
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

.monospace {
  font-family: 'Courier New', monospace;
  font-size: 13px;
  color: #606266;
}
</style>
