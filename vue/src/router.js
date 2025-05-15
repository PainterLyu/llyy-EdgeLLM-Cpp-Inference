import { createRouter, createWebHistory } from 'vue-router'
import ChatView from './views/ChatView.vue'
import MetricsView from './components/MetricsView.vue'

const routes = [
    {
        path: '/',
        name: 'Chat',
        component: ChatView
    },
    {
        path: '/metrics',
        name: 'Metrics',
        component: MetricsView
    }
]

const router = createRouter({
    history: createWebHistory(),
    routes
})

export default router 