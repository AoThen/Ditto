<template>
  <div class="settings-container">
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
    </el-card>

    <el-dialog v-model="changePasswordDialogVisible" title="修改加密密码" width="420px">
      <el-form label-width="100px">
        <el-form-item label="旧密码">
          <el-input
            v-model="oldPassword"
            type="password"
            placeholder="输入当前加密密码"
            show-password
          />
        </el-form-item>
        <el-form-item label="新密码">
          <el-input
            v-model="newPassword"
            type="password"
            placeholder="输入新加密密码"
            show-password
          />
        </el-form-item>
        <el-form-item label="确认新密码">
          <el-input
            v-model="confirmNewPassword"
            type="password"
            placeholder="再次输入新加密密码"
            show-password
          />
        </el-form-item>
        <el-form-item label="密码提示">
          <el-input v-model="newPasswordHint" placeholder="输入新的密码提示（可选）" />
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
import { getEncryptionSalt, getKeyMaterial, setupEncryption, disableEncryption, changeEncryptionPassword } from '@/api/clips'
import { deriveKEK, generateDEK, wrapDEK, unwrapDEK, computeVerificationHash } from '@/utils/crypto'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useUserStore } from '@/stores/user'
import axios from 'axios'

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
const oldPassword = ref('')
const newPassword = ref('')
const confirmNewPassword = ref('')
const newPasswordHint = ref('')
const changePasswordLoading = ref(false)

const maskedSalt = '****'

async function fetchSalt() {
  try {
    const res = await getEncryptionSalt()
    if (res.code === 0) {
      saltValue.value = res.data?.salt || ''
      encryptionEnabled.value = !!res.data?.encryption_enabled
    }
  } catch (err) {
    console.error('Failed to fetch encryption salt:', err)
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
    const saltRes = await getEncryptionSalt()
    if (saltRes.code !== 0 || !saltRes.data?.salt) {
      ElMessage.error('无法获取加密盐值')
      return
    }
    const saltB64 = saltRes.data.salt

    const DEK = await generateDEK()
    const KEK = await deriveKEK(encryptionPassword.value, saltB64)
    const wrappedDEK = await wrapDEK(KEK, DEK)
    const verificationHash = await computeVerificationHash(encryptionPassword.value, saltB64)

    await setupEncryption({
      wrapped_dek: wrappedDEK,
      verification_hash: verificationHash,
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
  oldPassword.value = ''
  newPassword.value = ''
  confirmNewPassword.value = ''
  newPasswordHint.value = ''
  changePasswordDialogVisible.value = true
}

async function handleChangePassword() {
  if (!oldPassword.value) {
    ElMessage.warning('请输入旧密码')
    return
  }
  if (!newPassword.value) {
    ElMessage.warning('请输入新密码')
    return
  }
  if (!confirmNewPassword.value) {
    ElMessage.warning('请再次输入新密码')
    return
  }
  if (newPassword.value !== confirmNewPassword.value) {
    ElMessage.error('两次输入的新密码不一致')
    return
  }
  if (newPassword.value.length < 8) {
    ElMessage.warning('加密密码至少需要 8 位')
    return
  }
  changePasswordLoading.value = true
  try {
    const keyMaterialRes = await getKeyMaterial()
    if (keyMaterialRes.code !== 0 || !keyMaterialRes.data?.wrapped_dek || !keyMaterialRes.data?.salt) {
      ElMessage.error('无法获取密钥材料')
      return
    }
    const { wrapped_dek: oldWrappedDEK, salt: oldSaltB64 } = keyMaterialRes.data

    const oldKEK = await deriveKEK(oldPassword.value, oldSaltB64)
    const DEK = await unwrapDEK(oldKEK, oldWrappedDEK)

    const newSaltBytes = crypto.getRandomValues(new Uint8Array(32))
    const newSaltB64 = btoa(String.fromCharCode(...newSaltBytes))

    const newKEK = await deriveKEK(newPassword.value, newSaltB64)
    const newWrappedDEK = await wrapDEK(newKEK, DEK)
    const oldVerificationHash = await computeVerificationHash(oldPassword.value, oldSaltB64)
    const newVerificationHash = await computeVerificationHash(newPassword.value, newSaltB64)

    const res = await changeEncryptionPassword({
      old_verification_hash: oldVerificationHash,
      new_salt: newSaltB64,
      new_wrapped_dek: newWrappedDEK,
      new_verification_hash: newVerificationHash,
      new_password_hint: newPasswordHint.value,
    })
    if (res.code === 0) {
      ElMessage.success('加密密码已更新')
      changePasswordDialogVisible.value = false
      fetchSalt()
    }
  } catch (err) {
    console.error('Failed to change password:', err)
    if (err.response?.data?.code === 40301) {
      ElMessage.error('旧密码验证失败，请重试')
    } else {
      ElMessage.error('修改加密密码失败')
    }
  } finally {
    changePasswordLoading.value = false
  }
}

async function handleLogout() {
  try {
    await ElMessageBox.confirm('确定退出登录吗？', '确认', {
      confirmButtonText: '确定', cancelButtonText: '取消', type: 'info',
    })
    try {
      await axios.post('/api/v1/auth/logout')
    } catch (e) {
      // ignore
    }
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