import { createRouter, createWebHistory } from 'vue-router'
import { useUserStore } from '../stores/user'

const routes = [
  {
    path: '/',
    redirect: '/login',
  },
  {
    path: '/login',
    name: 'Login',
    component: () => import('../views/Login.vue'),
    meta: { requiresAuth: false },
  },
  {
    path: '/dashboard',
    name: 'Dashboard',
    component: () => import('../views/Dashboard.vue'),
    meta: { requiresAuth: true, requiresWs: true },
    children: [
      { path: '', component: () => import('../views/StatsDashboard.vue') },
      { path: 'clips', component: () => import('../views/Clips.vue') },
      { path: 'groups', component: () => import('../views/Groups.vue') },
      { path: 'devices', component: () => import('../views/Devices.vue') },
      { path: 'sync-logs', component: () => import('../views/SyncLogs.vue') },
      { path: 'settings', component: () => import('../views/Settings.vue') },
      {
        path: 'admin/users',
        name: 'AdminUsers',
        component: () => import('../views/admin/Users.vue'),
        meta: { requiresAuth: true, requiresAdmin: true },
      },
    ],
  },
  // 404 catch-all route (must be last)
  {
    path: '/:pathMatch(.*)*',
    name: 'NotFound',
    component: () => import('../views/NotFound.vue'),
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

function isAuthenticated() {
  const userInfo = localStorage.getItem('userInfo')
  if (userInfo) {
    try {
      const parsed = JSON.parse(userInfo)
      if (parsed.device_id) return true
    } catch {
      // ignore
    }
  }
  const match = document.cookie.match(/(^| )device_id=([^;]+)/)
  return match ? true : false
}

function isAdmin() {
  const userInfo = localStorage.getItem('userInfo')
  if (userInfo) {
    try {
      const parsed = JSON.parse(userInfo)
      return parsed.role === 'admin'
    } catch {
      // ignore
    }
  }
  return false
}

router.beforeEach((to, from, next) => {
  if (to.meta.requiresAuth && !isAuthenticated()) {
    next('/login')
  } else if (to.path === '/login' && isAuthenticated()) {
    next('/dashboard')
  } else if (to.meta.requiresAdmin && !isAdmin()) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router