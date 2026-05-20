import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vite.dev/config/
export default defineConfig({
  plugins: [vue()],
  // 发布时使用绝对路径，由后端统一托管
  base: '/',
  build: {
    outDir: '../frontend-dist',
    emptyOutDir: true,
  },
  server: {
    port: 5173,
    proxy: {
      '^/api(/|$)': {
        target: 'http://127.0.0.1:8080',
        changeOrigin: true,
      },
    },
  },

})
