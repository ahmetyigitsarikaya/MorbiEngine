import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  base: './',
  plugins: [
    react(),
    viteSingleFile(),
    {
      name: 'strip-module-tags',
      enforce: 'post',
      transformIndexHtml(html) {
        return html
          .replace(/<script type="module" crossorigin>/g, '<script defer>')
          .replace(/<script type="module">/g, '<script defer>');
      }
    }
  ],
  server: {
    port: 5173,
    strictPort: true
  },
  build: {
    target: 'esnext',
    outDir: 'dist'
  }
});
