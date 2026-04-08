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
      { path: 'settings', component: () => import('../views/Settings.vue') },
    ],
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

router.beforeEach((to, from, next) => {
  const userStore = useUserStore()
  if (to.meta.requiresAuth && !userStore.token) {
    next('/login')
  } else if ((to.path === '/login' || to.path === '/register') && userStore.token) {
    next('/dashboard')
  } else {
    next()
  }
})

export default router
