<template>
  <section class="docs-shell">
    <RouterView />
  </section>
</template>

<script>
  export default {
    name: 'ApiManualView',
  }
</script>

<style scoped>
  .docs-shell {
    min-height: 70vh;
  }
</style>
