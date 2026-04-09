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
    path: '/register',
    name: 'Register',
    component: () => import('../views/Register.vue'),
    meta: { requiresAuth: false },
  },
  {
    path: '/dashboard',
    name: 'Dashboard',
    component: () => import('../views/Dashboard.vue'),
    meta: { requiresAuth: true, requiresWs: true },
    beforeEnter: (to, from, next) => {
      // WS connection will be handled by the composable in child components
      next()
    },
    children: [
      { path: '', redirect: '/dashboard' },
      { path: '', component: () => import('../views/StatsDashboard.vue') },
      { path: 'clips', component: () => import('../views/Clips.vue') },
      { path: 'groups', component: () => import('../views/Groups.vue') },
      { path: 'devices', component: () => import('../views/Devices.vue') },
      { path: 'sync-logs', component: () => import('../views/SyncLogs.vue') },
      { path: 'settings', component: () => import('../views/Settings.vue') },
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

// Helper: check if user is authenticated
// Uses localStorage check since HttpOnly cookies aren't accessible from JS
// and cross-port cookies may not be visible in some environments
function isAuthenticated() {
  // Check localStorage (set by setUserInfo after login)
  const userInfo = localStorage.getItem('userInfo')
  if (userInfo) {
    try {
      const parsed = JSON.parse(userInfo)
      if (parsed.device_id) return true
    } catch {
      // ignore parse errors
    }
  }
  // Fallback: check cookie (works in production where frontend/backend share origin)
  const match = document.cookie.match(/(^| )device_id=([^;]+)/)
  return match ? true : false
}

router.beforeEach((to, from, next) => {
  if (to.meta.requiresAuth && !isAuthenticated()) {
    next('/login')
  } else if ((to.path === '/login' || to.path === '/register') && isAuthenticated()) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router
