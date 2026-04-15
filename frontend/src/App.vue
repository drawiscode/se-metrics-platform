<template>
  <div class="layout">
    <header class="topbar">
      <div class="brand">DevInsight</div>
      <nav class="nav">
        <RouterLink to="/repos">Repos</RouterLink>
        <RouterLink to="/ai">AI</RouterLink>
      </nav>
    </header>

    <main class="content">
      <RouterView />
    </main>
  </div>
</template>

<style scoped>
  .layout { font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, "Microsoft YaHei", sans-serif; }
  .topbar { display:flex; gap:16px; align-items:center; padding:12px 16px; border-bottom:1px solid #ddd; }
  .brand { font-weight:700; }
  .nav { display:flex; gap:12px; }
  .content { padding:16px; }
  a.router-link-active { font-weight:700; }
</style>