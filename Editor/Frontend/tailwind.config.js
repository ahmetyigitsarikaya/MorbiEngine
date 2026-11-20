/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        morbi: {
          bg: '#0b0f17',
          panel: '#111827',
          border: '#1f2937',
          accent: '#38bdf8',
          emerald: '#10b981'
        }
      }
    }
  },
  plugins: []
};
