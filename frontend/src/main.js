import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'
import App from './App.vue'
import ReposView from './views/ReposView.vue'
import RepoDetailView from './views/RepoDetailView.vue'
import TasksView from './views/TasksView.vue'
import WeeklyReportsView from './views/WeeklyReportsView.vue' 
import ExpertsView from './views/ExpertsView.vue'
import QualityView from './views/QualityView.vue'
import SystemLogsView from './views/SystemLogsView.vue'
import ApiConsoleView from './views/ApiConsoleView.vue'
import ApiManualView from './views/ApiManualView.vue'
import ApiManualPageView from './views/ApiManualPageView.vue'

import AiView from './views/AiView.vue'
import AiConversationDetailView from './views/AiConversationDetailView.vue'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/repos' },
    { path: '/repos', component: ReposView },
    { path: '/repos/:id', component: RepoDetailView, props: true },
    { path: '/repos/:id/tasks', component: TasksView, props: true },
    { path: '/repos/:id/reports', component: WeeklyReportsView, props: true }, 
    { path: '/repos/:id/experts', component: ExpertsView, props: true },
    { path: '/repos/:id/quality', component: QualityView, props: true },
    { path: '/ai', component: AiView },
    { path: '/ai/conversations/:id', component: AiConversationDetailView, props: true },
    { path: '/system-logs', component: SystemLogsView },
    { path: '/api-console', component: ApiConsoleView },
    {
      path: '/api-manual',
      component: ApiManualView,
      children: [
        { path: '', redirect: '/api-manual/console/overview' },
        { path: ':section/:page', component: ApiManualPageView },
      ],
    },
  ],
})

createApp(App).use(router).mount('#app')