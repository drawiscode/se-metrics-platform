import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'
import App from './App.vue'
import ReposView from './views/ReposView.vue'
import RepoDetailView from './views/RepoDetailView.vue'
import TasksView from './views/TasksView.vue'
import WeeklyReportsView from './views/WeeklyReportsView.vue' 
import ExpertsView from './views/ExpertsView.vue'

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
    { path: '/ai', component: AiView },
    { path: '/ai/conversations/:id', component: AiConversationDetailView, props: true },
  ],
})

createApp(App).use(router).mount('#app')