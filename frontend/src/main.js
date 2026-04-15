import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'
import App from './App.vue'
import ReposView from './views/ReposView.vue'
import RepoDetailView from './views/RepoDetailView.vue'
import AiView from './views/AiView.vue'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/repos' },
    { path: '/repos', component: ReposView },
    { path: '/repos/:id', component: RepoDetailView, props: true },
    { path: '/ai', component: AiView },
  ],
})

createApp(App).use(router).mount('#app')