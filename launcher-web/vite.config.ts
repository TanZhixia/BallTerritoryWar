import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

// 开发时：Vite 在 3070（HMR），后端在 3071，/api 代理过去。
// 生产时：`npm run build` 出 dist/，后端在 3070 同端口服务静态资源 + API。
export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    port: 3070,
    strictPort: true,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:3071',
        changeOrigin: false,
      },
    },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
