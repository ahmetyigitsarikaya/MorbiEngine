import React, { useState } from 'react';
import {
  FolderIcon,
  FilmIcon,
  TerminalIcon,
  SearchIcon,
  ChevronDownIcon,
  ChevronRightIcon,
  DockIcon
} from './Icons';
import { AssetItem } from '../types';

interface NativeFile extends File {
  path?: string;
}

interface AssetBrowserPanelProps {
  folders?: string[];
  assets?: AssetItem[];
  onSelectAsset?: (asset: AssetItem) => void;
  onOpenScene?: (path: string) => void;
  onImportModel?: () => void;
  onImportModelPath?: (path: string) => void;
  onDropAsset?: (asset: AssetItem, normX: number, normY: number) => void;
}

export const AssetBrowserPanel: React.FC<AssetBrowserPanelProps> = ({
  folders = ['Models', 'Textures', 'Scenes', 'Scripts'],
  assets = [],
  onSelectAsset,
  onOpenScene,
  onImportModel,
  onImportModelPath,
  onDropAsset
}) => {
  const [activeTab, setActiveTab] = useState<'Asset Browser' | 'Console' | 'Animation'>('Asset Browser');
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedFolder, setSelectedFolder] = useState('Assets');

  const getAssetBadge = (type: string, name: string) => {
    if (type === 'model') return 'MESH';
    if (type === 'texture') return 'TEX2D';
    if (type === 'scene') return 'SCENE';
    if (type === 'script') return 'CS';
    if (type === 'prefab') return 'PREFAB';
    const ext = name.split('.').pop()?.toUpperCase();
    return ext || 'FILE';
  };

  const renderAssetIcon = (type: string) => {
    switch (type) {
      case 'model':
        return (
          <div className="w-10 h-10 rounded bg-indigo-950/60 border border-indigo-800/60 flex items-center justify-center text-indigo-300">
            <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polygon points="12 2 2 7 12 12 22 7 12 2" />
              <polyline points="2 17 12 22 22 17" />
              <polyline points="2 12 12 17 22 12" />
            </svg>
          </div>
        );
      case 'texture':
        return (
          <div className="w-10 h-10 rounded bg-emerald-900/60 border border-emerald-700/60 flex items-center justify-center text-emerald-300">
            <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <rect x="3" y="3" width="18" height="18" rx="2" />
              <circle cx="8.5" cy="8.5" r="1.5" />
              <polyline points="21 15 16 10 5 21" />
            </svg>
          </div>
        );
      case 'scene':
        return (
          <div className="w-10 h-10 rounded bg-cyan-950/60 border border-cyan-800/60 flex items-center justify-center text-cyan-300">
            <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polygon points="12 2 2 7 12 12 22 7 12 2" />
              <polyline points="2 17 12 22 22 17" />
            </svg>
          </div>
        );
      case 'script':
        return (
          <div className="w-10 h-10 rounded bg-amber-950/60 border border-amber-800/60 flex items-center justify-center text-amber-300">
            <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polyline points="16 18 22 12 16 6" />
              <polyline points="8 6 2 12 8 18" />
            </svg>
          </div>
        );
      default:
        return (
          <div className="w-10 h-10 rounded bg-slate-900 border border-slate-700 flex items-center justify-center text-slate-400">
            <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
              <polyline points="14 2 14 8 20 8" />
            </svg>
          </div>
        );
    }
  };

  const filteredAssets = assets.filter((asset) => {
    const matchesSearch = searchQuery
      ? asset.name.toLowerCase().includes(searchQuery.toLowerCase())
      : true;
    if (!matchesSearch) return false;
    if (selectedFolder === 'Assets') return true;
    return asset.path.toLowerCase().includes(selectedFolder.toLowerCase());
  });

  return (
    <div className="flex-1 bg-[#0c1017] border-r border-[#19202c] flex flex-col select-none overflow-hidden">
      {/* Header Tabs */}
      <div className="h-8 px-2 bg-[#090c12] border-b border-[#19202c] flex items-center justify-between text-[11px]">
        <div className="flex items-center gap-1">
          <button
            onClick={() => setActiveTab('Asset Browser')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors font-medium ${
              activeTab === 'Asset Browser'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <FolderIcon size={12} />
            <span>Asset Browser ({assets.length})</span>
          </button>
          <button
            onClick={() => setActiveTab('Console')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors ${
              activeTab === 'Console'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <TerminalIcon size={12} />
            <span>Console</span>
          </button>
          <button
            onClick={() => setActiveTab('Animation')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors ${
              activeTab === 'Animation'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <FilmIcon size={12} />
            <span>Animation</span>
          </button>
        </div>

        <button className="p-1 text-slate-400 hover:text-white rounded hover:bg-slate-800">
          <DockIcon size={11} />
        </button>
      </div>

      {/* Main Asset Browser Layout (Folder Tree on Left, Assets Grid on Right) */}
      <div className="flex-1 flex overflow-hidden">
        {/* Left Folder Tree */}
        <div className="w-32 bg-[#090c12] border-r border-[#19202c] py-1 overflow-y-auto shrink-0 text-[11px] text-slate-400">
          <div
            onClick={() => setSelectedFolder('Assets')}
            className={`flex items-center gap-1 px-2 py-1 cursor-pointer hover:text-slate-200 ${
              selectedFolder === 'Assets' ? 'text-indigo-400 font-medium bg-[#141b27]' : ''
            }`}
          >
            <ChevronDownIcon size={10} />
            <FolderIcon size={12} className="text-amber-400" />
            <span>Assets</span>
          </div>

          <div className="pl-4">
            {folders.map((folder) => (
              <div
                key={folder}
                onClick={() => setSelectedFolder(folder)}
                className={`flex items-center gap-1 px-2 py-0.5 cursor-pointer hover:text-slate-200 ${
                  selectedFolder === folder ? 'text-indigo-400 font-medium bg-[#141b27]' : ''
                }`}
              >
                <ChevronRightIcon size={9} />
                <FolderIcon size={11} className="text-slate-400" />
                <span className="truncate">{folder}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Right Asset View Container */}
        <div className="flex-1 flex flex-col min-w-0 bg-[#0c1017]">
          {/* Top Search & Filter Bar */}
          <div className="h-8 px-2.5 border-b border-[#19202c] flex items-center justify-between gap-2 bg-[#0a0d14]">
            <div className="flex items-center gap-2 flex-1">
              <button
                onClick={() => onImportModel?.()}
                title="Import external 3D Model (.fbx, .obj, .gltf, .glb, .dae, .stl)"
                className="flex items-center gap-1.5 px-2.5 py-1 bg-indigo-600 hover:bg-indigo-500 text-white rounded text-[11px] font-medium shadow-sm transition-colors cursor-pointer shrink-0"
              >
                <span>📥</span>
                <span>Import Model</span>
              </button>
              <div className="relative flex-1 max-w-xs flex items-center">
                <SearchIcon size={10} className="absolute left-2 text-slate-500 pointer-events-none" />
                <input
                  type="text"
                  aria-label="Search Assets"
                  placeholder="Search Assets..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  className="w-full bg-[#121822] text-slate-200 placeholder-slate-500 text-[10px] pl-5 pr-2 py-0.5 rounded border border-[#1e2738] focus:outline-none focus:border-indigo-500"
                />
              </div>
            </div>
            <div className="text-[10px] text-slate-500 font-mono shrink-0">
              Assets &gt; {selectedFolder} ({filteredAssets.length} items)
            </div>
          </div>

          {/* Grid of Real Asset Thumbnails */}
          <div
            onDragOver={(e) => {
              e.preventDefault();
              e.dataTransfer.dropEffect = 'copy';
            }}
            onDrop={(e) => {
              e.preventDefault();
              if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
                for (let i = 0; i < e.dataTransfer.files.length; i++) {
                  const file = e.dataTransfer.files[i];
                  const ext = file.name.split('.').pop()?.toLowerCase();
                  if (['fbx', 'obj', 'gltf', 'glb', 'dae', 'stl', 'blend', '3ds', 'ply'].includes(ext || '')) {
                    const fullPath = (file as NativeFile).path || file.name;
                    onImportModelPath?.(fullPath);
                  }
                }
              }
            }}
            className="flex-1 p-2.5 overflow-y-auto grid grid-cols-4 sm:grid-cols-6 lg:grid-cols-6 gap-2 content-start"
          >
            {filteredAssets.length === 0 ? (
              <div className="col-span-full py-8 text-center text-xs text-slate-500">
                No assets found in {selectedFolder}
              </div>
            ) : (
              filteredAssets.map((asset) => {
                const isSceneAsset = asset.type === 'scene'
                  || asset.name.toLowerCase().endsWith('.mscene.json')
                  || asset.path.toLowerCase().endsWith('.mscene.json');
                return (
                <div
                  key={asset.id}
                  draggable={true}
                  onDragStart={(e) => {
                    const str = JSON.stringify(asset);
                    e.dataTransfer.setData('application/json', str);
                    e.dataTransfer.setData('text/plain', str);
                    e.dataTransfer.effectAllowed = 'copy';
                  }}
                  onDragEnd={(e) => {
                    // Check if released within 3D Viewport element bounds
                    const vpElem = document.querySelector('[data-viewport-surface="true"]');
                    if (vpElem) {
                      const rect = vpElem.getBoundingClientRect();
                      if (e.clientX >= rect.left && e.clientX <= rect.right &&
                          e.clientY >= rect.top && e.clientY <= rect.bottom) {
                        const normX = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
                        const normY = Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height));
                        onDropAsset?.(asset, normX, normY);
                      }
                    }
                  }}
                  onDoubleClick={() => {
                    if (isSceneAsset) {
                      onOpenScene?.(asset.path);
                    } else {
                      onDropAsset?.(asset, 0.5, 0.5);
                    }
                  }}
                  onClick={() => onSelectAsset?.(asset)}
                  className="group flex flex-col items-center justify-center p-2 rounded bg-[#101622] hover:bg-[#162030] border border-[#1c2637] hover:border-indigo-500/60 cursor-grab active:cursor-grabbing transition-all shadow-sm relative select-none"
                  title={isSceneAsset ? 'Double-Click to Open Scene' : 'Drag into 3D Viewport or Double-Click to Add to Scene'}
                >
                  <span className="absolute top-1 right-1 px-1 py-0.5 rounded text-[8px] font-mono bg-slate-800 text-slate-400">
                    {getAssetBadge(asset.type, asset.name)}
                  </span>
                  <div className="w-12 h-12 flex items-center justify-center mb-1 group-hover:scale-105 transition-transform mt-1">
                    {renderAssetIcon(asset.type)}
                  </div>
                  <span className="text-[10px] text-slate-300 text-center truncate w-full group-hover:text-white" title={asset.path}>
                    {asset.name}
                  </span>
                  {asset.sizeBytes ? (
                    <span className="text-[8px] text-slate-500 font-mono">
                      {(asset.sizeBytes / 1024).toFixed(1)} KB
                    </span>
                  ) : null}
                </div>
                );
              })
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
