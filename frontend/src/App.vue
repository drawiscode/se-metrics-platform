<template>
  <div class="layout">
    <aside class="sidebar">
      <div class="brand">DevInsight</div>
      <nav class="nav">
        <RouterLink to="/repos">仓库管理</RouterLink>
        <RouterLink to="/ai">AI 问答</RouterLink>
        <RouterLink to="/system-logs">系统日志</RouterLink>
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

  .nav a {
    color: #dbe8ff;
    text-decoration: none;
    padding: 8px 10px;
    border-radius: 8px;
  }

  .nav a.router-link-active {
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