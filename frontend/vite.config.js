import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    host: true,
    proxy: {
      '/device': { target: 'http://127.0.0.1:8001', changeOrigin: true },
      '/test': { target: 'http://127.0.0.1:8001', changeOrigin: true },
      '/health': { target: 'http://127.0.0.1:8001', changeOrigin: true },
      '/sensor': { target: 'http://127.0.0.1:8001', changeOrigin: true },
    },
  },
})
