import React from 'react';
import ReactDOM from 'react-dom/client';
import './index.css';
import { App } from './App';

function mount() {
  const container = document.getElementById('root');
  if (!container) {
    window.addEventListener('DOMContentLoaded', mount);
    return;
  }
  const root = ReactDOM.createRoot(container);
  root.render(
    <React.StrictMode>
      <App />
    </React.StrictMode>
  );
  if (typeof window !== 'undefined' && window.chrome?.webview) {
    window.chrome.webview.postMessage('JS_REACT_MOUNTED: MorbiEditor AAA UI active');
  }
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', mount);
} else {
  mount();
}
