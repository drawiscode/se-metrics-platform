<template>
  <div class="layout">
    <aside class="sidebar">
      <div class="brand">DevInsight</div>
      <nav class="nav">
        <RouterLink to="/repos">仓库管理</RouterLink>
        <RouterLink to="/ai">AI 问答</RouterLink>
        <RouterLink to="/system-logs">系统日志</RouterLink>
        <RouterLink to="/api-console">API 控制台</RouterLink>

        <button class="nav-group-btn" type="button" @click="toggleDocs">
          <span>API 接口文档</span>
          <span class="chev" :class="{ open: docsOpen }">▸</span>
        </button>
        <div v-if="docsOpen" class="nav-sub">
          <div v-for="group in navGroups" :key="group.id" class="nav-group">
            <div class="nav-group-title">{{ group.title }}</div>
            <RouterLink
              v-for="item in group.items"
              :key="item.path"
              class="nav-item"
              :class="{ active: isActive(item.path) }"
              :to="item.path"
            >
              {{ item.title }}
            </RouterLink>
          </div>
        </div>
      </nav>
    </aside>

    <section class="main-pane">
      <header class="topbar">平台控制台</header>
      <main class="content">
        <RouterView />
      </main>
    </section>
  </div>
</template>

<script>
  const navGroups = [
    {
      id: 'console',
      title: '控制台指南',
      items: [
        { title: 'API 控制台使用', path: '/api-manual/console/overview' },
      ],
    },
    {
      id: 'repo',
      title: '仓库与同步',
      items: [
        { title: '仓库基础', path: '/api-manual/repo/basic' },
        { title: '指标与健康', path: '/api-manual/repo/metrics' },
        { title: '活动与 CI', path: '/api-manual/repo/activity-ci' },
        { title: '仓库介绍', path: '/api-manual/repo/intro' },
        { title: '同步接口', path: '/api-manual/sync/overview' },
      ],
    },
    {
      id: 'hotspots',
      title: '热点与专家',
      items: [
        { title: '热点分析', path: '/api-manual/hotspots/overview' },
        { title: '隐形专家', path: '/api-manual/experts/overview' },
      ],
    },
    {
      id: 'quality',
      title: '质量与任务',
      items: [
        { title: '质量分析', path: '/api-manual/quality/analysis' },
        { title: '质量任务与运行', path: '/api-manual/quality/tasks' },
        { title: '质量基线与问题', path: '/api-manual/quality/baseline' },
        { title: '任务清单', path: '/api-manual/tasks/overview' },
      ],
    },
    {
      id: 'report-ai',
      title: '报表与 AI',
      items: [
        { title: '周报接口', path: '/api-manual/reports/overview' },
        { title: '知识库与问答', path: '/api-manual/ai/overview' },
        { title: '代码索引', path: '/api-manual/code/index' },
      ],
    },
    {
      id: 'risk-system',
      title: '风险与系统',
      items: [
        { title: '风险扫描', path: '/api-manual/risk/overview' },
        { title: '系统日志', path: '/api-manual/system/logs' },
      ],
    },
  ]

  export default {
    name: 'App',
    data() {
      return {
        navGroups,
        docsOpen: false,
      }
    },
    watch: {
      '$route.path': {
        immediate: true,
        handler(path) {
          if (String(path || '').startsWith('/api-manual')) {
            this.docsOpen = true
          }
        },
      },
    },
    methods: {
      toggleDocs() {
        this.docsOpen = !this.docsOpen
        if (this.docsOpen && !String(this.$route.path || '').startsWith('/api-manual')) {
          this.$router.push('/api-manual/console/overview')
        }
      },
      isActive(path) {
        return this.$route.path === path
      },
    },
  }
</script>

<style scoped>
  .layout {
    font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
    min-height: 100vh;
    display: grid;
    grid-template-columns: 220px 1fr;
    background: #f4f7fb;
  }

  .sidebar {
    background: #10263f;
    color: #eaf1ff;
    padding: 16px 14px;
  }

  .brand {
    font-weight: 700;
    font-size: 18px;
    margin-bottom: 14px;
    letter-spacing: 0.4px;
  }

  .nav {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .nav a,
  .nav button {
    color: #dbe8ff;
    text-decoration: none;
    padding: 8px 10px;
    border-radius: 8px;
  }

  .nav button {
    background: transparent;
    border: none;
    text-align: left;
    cursor: pointer;
    font: inherit;
  }

  .nav a.router-link-active {
    background: #21476e;
    color: #ffffff;
    font-weight: 700;
  }

  .nav-group-btn {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .chev {
    display: inline-block;
    transition: transform 0.2s ease;
  }

  .chev.open {
    transform: rotate(90deg);
  }

  .nav-sub {
    margin-left: 10px;
    border-left: 1px solid rgba(255, 255, 255, 0.1);
    padding-left: 8px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .nav-group-title {
    font-size: 12px;
    font-weight: 700;
    color: #9fb6d8;
    text-transform: none;
    padding: 4px 6px;
  }

  .nav-item {
    display: block;
    color: #dbe8ff;
    text-decoration: none;
    padding: 6px 8px;
    border-radius: 8px;
    font-size: 13px;
  }

  .nav-item.active {
    background: #21476e;
    color: #ffffff;
    font-weight: 700;
  }

  .main-pane {
    display: grid;
    grid-template-rows: auto 1fr;
    min-width: 0;
  }

  .topbar {
    display: flex;
    align-items: center;
    padding: 12px 18px;
    border-bottom: 1px solid #d9e3f2;
    background: #ffffff;
    font-weight: 600;
  }

  .content {
    padding: 16px;
    min-width: 0;
  }

  @media (max-width: 900px) {
    .layout {
      grid-template-columns: 1fr;
      grid-template-rows: auto 1fr;
    }

    .sidebar {
      display: grid;
      grid-template-columns: auto 1fr;
      align-items: center;
      gap: 12px;
    }

    .brand {
      margin: 0;
    }

    .nav {
      flex-direction: row;
      flex-wrap: wrap;
    }
  }
</style>