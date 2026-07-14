<template>
  <div class="users-page">
    <div class="page-header">
      <h3>用户管理</h3>
      <el-button type="primary" @click="showCreateDialog = true">创建用户</el-button>
    </div>

    <!-- Search -->
    <el-input
      v-model="search"
      placeholder="搜索用户名或邮箱"
      clearable
      class="search-input"
      @input="handleSearch"
    />

    <!-- User Table -->
    <el-table :data="users" v-loading="loading" stripe style="width: 100%">
      <el-table-column prop="id" label="ID" width="80" />
      <el-table-column prop="username" label="用户名" min-width="140" />
      <el-table-column prop="email" label="邮箱" min-width="200" />
      <el-table-column prop="role" label="角色" width="100">
        <template #default="{ row }">
          <el-tag :type="row.role === 'admin' ? 'danger' : 'info'" size="small">
            {{ row.role === 'admin' ? '管理员' : '用户' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="is_active" label="状态" width="100">
        <template #default="{ row }">
          <el-tag :type="row.is_active ? 'success' : 'danger'" size="small">
            {{ row.is_active ? '启用' : '禁用' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="device_count" label="设备数" width="80" />
      <el-table-column prop="created_at" label="注册时间" min-width="160">
        <template #default="{ row }">
          {{ formatDate(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="220" fixed="right">
        <template #default="{ row }">
          <el-button size="small" @click="handleEdit(row)">编辑</el-button>
          <el-button size="small" @click="handleResetPassword(row)">重置密码</el-button>
          <el-popconfirm
            title="确认删除该用户？将同时删除该用户的所有剪贴板数据和设备记录，不可恢复。"
            @confirm="handleDelete(row)"
          >
            <template #reference>
              <el-button size="small" type="danger">删除</el-button>
            </template>
          </el-popconfirm>
        </template>
      </el-table-column>
    </el-table>

    <!-- Pagination -->
    <el-pagination
      v-model:current-page="page"
      v-model:page-size="perPage"
      :total="total"
      layout="total, prev, pager, next"
      background
      class="pagination"
      @current-change="fetchUsers"
    />

    <!-- Create User Dialog -->
    <el-dialog v-model="showCreateDialog" title="创建用户" width="450px">
      <el-form ref="createFormRef" :model="createForm" :rules="createRules" label-position="top">
        <el-form-item label="用户名" prop="username">
          <el-input v-model="createForm.username" placeholder="3-32个字符" />
        </el-form-item>
        <el-form-item label="邮箱" prop="email">
          <el-input v-model="createForm.email" placeholder="user@example.com" />
        </el-form-item>
        <el-form-item label="密码" prop="password">
          <el-input v-model="createForm.password" type="password" placeholder="至少6位密码" show-password />
        </el-form-item>
        <el-form-item label="确认密码" prop="confirmPassword">
          <el-input v-model="createForm.confirmPassword" type="password" placeholder="再次输入密码" show-password />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showCreateDialog = false">取消</el-button>
        <el-button type="primary" :loading="creating" @click="handleCreate">创建</el-button>
      </template>
    </el-dialog>

    <!-- Edit User Dialog -->
    <el-dialog v-model="showEditDialog" title="编辑用户" width="450px">
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-position="top">
        <el-form-item label="邮箱" prop="email">
          <el-input v-model="editForm.email" placeholder="user@example.com" />
        </el-form-item>
        <el-form-item label="状态">
          <el-switch v-model="editForm.is_active" active-text="启用" inactive-text="禁用" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showEditDialog = false">取消</el-button>
        <el-button type="primary" :loading="editing" @click="handleEditSubmit">保存</el-button>
      </template>
    </el-dialog>

    <!-- Reset Password Dialog -->
    <el-dialog v-model="showResetPwdDialog" title="重置密码" width="400px">
      <el-form ref="resetPwdFormRef" :model="resetPwdForm" :rules="resetPwdRules" label-position="top">
        <el-form-item label="新密码" prop="password">
          <el-input v-model="resetPwdForm.password" type="password" placeholder="至少6位密码" show-password />
        </el-form-item>
        <el-form-item label="确认密码" prop="confirmPassword">
          <el-input v-model="resetPwdForm.confirmPassword" type="password" placeholder="再次输入密码" show-password />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showResetPwdDialog = false">取消</el-button>
        <el-button type="primary" :loading="resetting" @click="handleResetPwdSubmit">确认重置</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { getUsers, createUser, updateUser, deleteUser, resetPassword } from '@/api/admin'
import { ElMessage } from 'element-plus'

const loading = ref(false)
const users = ref([])
const total = ref(0)
const page = ref(1)
const perPage = ref(20)
const search = ref('')

let searchTimer = null

// Create dialog
const showCreateDialog = ref(false)
const creating = ref(false)
const createFormRef = ref(null)
const createForm = ref({ username: '', email: '', password: '', confirmPassword: '' })
const createRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 3, max: 32, message: '用户名长度3-32位', trigger: 'blur' },
  ],
  email: [
    { required: true, message: '请输入邮箱', trigger: 'blur' },
    { type: 'email', message: '请输入有效邮箱', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码至少6位', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { validator: (rule, value, callback) => {
      if (value !== createForm.value.password) callback(new Error('两次密码不一致'))
      else callback()
    }, trigger: 'blur' },
  ],
}

// Edit dialog
const showEditDialog = ref(false)
const editing = ref(false)
const editFormRef = ref(null)
const editForm = ref({ id: null, email: '', is_active: true })
const editRules = {
  email: [
    { required: true, message: '请输入邮箱', trigger: 'blur' },
    { type: 'email', message: '请输入有效邮箱', trigger: 'blur' },
  ],
}

// Reset password dialog
const showResetPwdDialog = ref(false)
const resetting = ref(false)
const resetPwdFormRef = ref(null)
const resetPwdForm = ref({ userId: null, password: '', confirmPassword: '' })
const resetPwdRules = {
  password: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 6, message: '密码至少6位', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { validator: (rule, value, callback) => {
      if (value !== resetPwdForm.value.password) callback(new Error('两次密码不一致'))
      else callback()
    }, trigger: 'blur' },
  ],
}

function formatDate(dateStr) {
  if (!dateStr) return ''
  return new Date(dateStr).toLocaleString('zh-CN', {
    year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit',
  })
}

function handleSearch() {
  clearTimeout(searchTimer)
  searchTimer = setTimeout(() => {
    page.value = 1
    fetchUsers()
  }, 300)
}

async function fetchUsers() {
  loading.value = true
  try {
    const res = await getUsers({ page: page.value, per_page: perPage.value, search: search.value })
    if (res.code === 0) {
      users.value = res.data.items
      total.value = res.data.total
    } else {
      ElMessage.error(res.message || '获取用户列表失败')
    }
  } catch (err) {
    ElMessage.error('获取用户列表失败')
  } finally {
    loading.value = false
  }
}

async function handleCreate() {
  if (!createFormRef.value) return
  await createFormRef.value.validate(async (valid) => {
    if (!valid) return
    creating.value = true
    try {
      const { confirmPassword, ...data } = createForm.value
      const res = await createUser(data)
      if (res.code === 0) {
        ElMessage.success('用户创建成功')
        showCreateDialog.value = false
        createForm.value = { username: '', email: '', password: '', confirmPassword: '' }
        fetchUsers()
      } else {
        ElMessage.error(res.message || '创建失败')
      }
    } catch (err) {
      ElMessage.error('创建用户失败')
    } finally {
      creating.value = false
    }
  })
}

function handleEdit(row) {
  editForm.value = { id: row.id, email: row.email, is_active: row.is_active }
  showEditDialog.value = true
}

async function handleEditSubmit() {
  if (!editFormRef.value) return
  await editFormRef.value.validate(async (valid) => {
    if (!valid) return
    editing.value = true
    try {
      const res = await updateUser(editForm.value.id, {
        email: editForm.value.email,
        is_active: editForm.value.is_active,
      })
      if (res.code === 0) {
        ElMessage.success('更新成功')
        showEditDialog.value = false
        fetchUsers()
      } else {
        ElMessage.error(res.message || '更新失败')
      }
    } catch (err) {
      ElMessage.error('更新用户失败')
    } finally {
      editing.value = false
    }
  })
}

function handleResetPassword(row) {
  resetPwdForm.value = { userId: row.id, password: '', confirmPassword: '' }
  showResetPwdDialog.value = true
}

async function handleResetPwdSubmit() {
  if (!resetPwdFormRef.value) return
  await resetPwdFormRef.value.validate(async (valid) => {
    if (!valid) return
    resetting.value = true
    try {
      const res = await resetPassword(resetPwdForm.value.userId, {
        password: resetPwdForm.value.password,
      })
      if (res.code === 0) {
        ElMessage.success('密码已重置')
        showResetPwdDialog.value = false
      } else {
        ElMessage.error(res.message || '重置失败')
      }
    } catch (err) {
      ElMessage.error('重置密码失败')
    } finally {
      resetting.value = false
    }
  })
}

async function handleDelete(row) {
  try {
    const res = await deleteUser(row.id)
    if (res.code === 0) {
      ElMessage.success('用户已删除')
      fetchUsers()
    } else {
      ElMessage.error(res.message || '删除失败')
    }
  } catch (err) {
    ElMessage.error('删除用户失败')
  }
}

onMounted(fetchUsers)
</script>

<style scoped>
.users-page {
  padding: 20px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.search-input {
  width: 300px;
  margin-bottom: 16px;
}

.pagination {
  margin-top: 20px;
  justify-content: center;
}
</style>