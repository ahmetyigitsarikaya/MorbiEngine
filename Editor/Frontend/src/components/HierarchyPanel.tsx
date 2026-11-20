import React, { useState } from 'react';
import { nativeBridge } from '../bridge/nativeBridge';
import {
  LayersIcon,
  PlusIcon,
  DockIcon,
  CollapseIcon,
  SearchIcon,
  ChevronDownIcon,
  ChevronRightIcon,
  FolderIcon,
  CameraIcon,
  SunIcon,
  CloudIcon,
  MountainIcon,
  UserIcon,
  AxisIcon,
  CubeIcon
} from './Icons';
import { HierarchyItem } from '../types';

interface HierarchyPanelProps {
  items: HierarchyItem[];
  selectedId: string;
  onSelectEntity: (item: HierarchyItem) => void;
  onCreateEntity?: (name: string, entityType: string) => void;
  onDeleteEntity?: (entityId: number) => void;
}

export const HierarchyPanel: React.FC<HierarchyPanelProps> = ({
  items,
  selectedId,
  onSelectEntity,
  onCreateEntity,
  onDeleteEntity
}) => {
  const [searchQuery, setSearchQuery] = useState('');
  const [createMenuOpen, setCreateMenuOpen] = useState(false);
  const createMenuContainerRef = React.useRef<HTMLDivElement>(null);
  const createMenuDropdownRef = React.useRef<HTMLDivElement>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; item: HierarchyItem } | null>(null);
  const contextMenuRef = React.useRef<HTMLDivElement>(null);

  React.useLayoutEffect(() => {
    const targetEl = (createMenuOpen && createMenuDropdownRef.current)
      ? createMenuDropdownRef.current
      : (contextMenu && contextMenuRef.current)
      ? contextMenuRef.current
      : null;

    if (targetEl) {
      const rect = targetEl.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      nativeBridge.sendMenuState(true, {
        x: Math.max(0, Math.round((rect.left - 4) * dpr)),
        y: Math.max(0, Math.round((rect.top - 2) * dpr)),
        width: Math.round((rect.width + 8) * dpr),
        height: Math.round((rect.height + 8) * dpr)
      }, 'hierarchy');
    } else {
      nativeBridge.sendMenuState(false, undefined, 'hierarchy');
    }

    return () => {
      nativeBridge.sendMenuState(false, undefined, 'hierarchy');
    };
  }, [createMenuOpen, contextMenu]);

  React.useEffect(() => {
    if (!createMenuOpen && !contextMenu) return;

    const handleOutsideClick = (e: MouseEvent) => {
      if (createMenuContainerRef.current && !createMenuContainerRef.current.contains(e.target as Node)) {
        setCreateMenuOpen(false);
      }
      if (contextMenuRef.current && !contextMenuRef.current.contains(e.target as Node)) {
        setContextMenu(null);
      }
    };

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setCreateMenuOpen(false);
        setContextMenu(null);
      }
    };

    window.addEventListener('pointerdown', handleOutsideClick);
    window.addEventListener('keydown', handleKeyDown);

    return () => {
      window.removeEventListener('pointerdown', handleOutsideClick);
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, [createMenuOpen, contextMenu]);

  const [expandedNodes, setExpandedNodes] = useState<Record<string, boolean>>({
    world: true,
    environment: true,
    player: true,
    npcs: false,
    buildings: false,
    ui: false
  });

  const toggleExpand = (id: string, e: React.MouseEvent) => {
    e.stopPropagation();
    setExpandedNodes((prev) => ({ ...prev, [id]: !prev[id] }));
  };

  const renderIcon = (iconType?: string) => {
    switch (iconType) {
      case 'camera':
        return <CameraIcon className="w-3.5 h-3.5 text-sky-400" />;
      case 'light':
        return <SunIcon className="w-3.5 h-3.5 text-amber-300" />;
      case 'cloud':
        return <CloudIcon className="w-3.5 h-3.5 text-cyan-300" />;
      case 'mountain':
        return <MountainIcon className="w-3.5 h-3.5 text-emerald-400" />;
      case 'avatar':
        return <UserIcon className="w-3.5 h-3.5 text-indigo-300" />;
      case 'pivot':
        return <AxisIcon className="w-3.5 h-3.5 text-purple-400" />;
      case 'folder':
        return <FolderIcon className="w-3.5 h-3.5 text-slate-400" />;
      default:
        return <CubeIcon className="w-3.5 h-3.5 text-slate-400" />;
    }
  };

  const renderNode = (node: HierarchyItem, depth = 0) => {
    const isExpanded = expandedNodes[node.id] ?? false;
    const isSelected = selectedId === node.id;
    const hasChildren = node.children && node.children.length > 0;

    if (
      searchQuery &&
      !node.name.toLowerCase().includes(searchQuery.toLowerCase()) &&
      !node.children?.some((c) => c.name.toLowerCase().includes(searchQuery.toLowerCase()))
    ) {
      return null;
    }

    return (
      <div key={node.id} className="select-none text-[11px]">
        <div
          onClick={() => onSelectEntity(node)}
          onContextMenu={(e) => {
            e.preventDefault();
            e.stopPropagation();
            setCreateMenuOpen(false);
            onSelectEntity(node);
            setContextMenu({ x: e.clientX, y: e.clientY, item: node });
          }}
          style={{ paddingLeft: `${depth * 12 + 6}px` }}
          className={`group flex items-center justify-between py-1 pr-2 cursor-pointer transition-colors ${
            isSelected
              ? 'bg-indigo-600 text-white font-medium rounded-sm'
              : 'text-slate-300 hover:bg-[#151c27] hover:text-white'
          }`}
        >
          <div className="flex items-center gap-1.5 truncate">
            {hasChildren ? (
              <button
                onClick={(e) => toggleExpand(node.id, e)}
                className="w-3.5 h-3.5 flex items-center justify-center text-slate-400 hover:text-slate-200"
              >
                {isExpanded ? <ChevronDownIcon size={10} /> : <ChevronRightIcon size={10} />}
              </button>
            ) : (
              <span className="w-3.5" />
            )}

            {renderIcon(node.iconType)}
            <span className="truncate">{node.name}</span>
          </div>

          {node.entityId !== undefined && onDeleteEntity && (
            <button
              onClick={(e) => {
                e.stopPropagation();
                onDeleteEntity(node.entityId!);
              }}
              title="Delete Entity"
              className="opacity-0 group-hover:opacity-100 hover:text-rose-400 text-slate-400 p-0.5 rounded transition-opacity"
            >
              ✕
            </button>
          )}
        </div>

        {hasChildren && isExpanded && (
          <div>{node.children!.map((child) => renderNode(child, depth + 1))}</div>
        )}
      </div>
    );
  };

  return (
    <div className="w-60 bg-[#0c1017] border-r border-[#19202c] flex flex-col select-none shrink-0">
      {/* Header */}
      <div className="h-8 px-2.5 border-b border-[#19202c] flex items-center justify-between text-slate-300">
        <div className="flex items-center gap-1.5 font-semibold text-[11px] text-slate-200">
          <LayersIcon size={13} className="text-indigo-400" />
          <span>Hierarchy</span>
        </div>
        <div className="flex items-center gap-1 text-slate-400 relative" ref={createMenuContainerRef}>
          <button
            onClick={() => {
              setContextMenu(null);
              setCreateMenuOpen(!createMenuOpen);
            }}
            title="Create Entity (+)"
            className="p-1 hover:text-white hover:bg-slate-800 rounded transition-colors text-indigo-400 hover:text-indigo-300"
          >
            <PlusIcon size={12} />
          </button>

          {/* Create Entity Dropdown Menu */}
          {createMenuOpen && (
            <div ref={createMenuDropdownRef} className="absolute top-full right-0 mt-1 w-44 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
              <button
                onClick={() => {
                  setCreateMenuOpen(false);
                  onCreateEntity?.('EmptyEntity', 'empty');
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white text-left text-xs transition-colors"
              >
                <span>📄</span>
                <span>Create Empty</span>
              </button>
              <button
                onClick={() => {
                  setCreateMenuOpen(false);
                  onCreateEntity?.('CubeMesh', 'cube');
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white text-left text-xs transition-colors"
              >
                <span>🧊</span>
                <span>3D Cube Object</span>
              </button>
              <button
                onClick={() => {
                  setCreateMenuOpen(false);
                  onCreateEntity?.('PointLight', 'light');
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white text-left text-xs transition-colors"
              >
                <span>💡</span>
                <span>Point Light</span>
              </button>
              <button
                onClick={() => {
                  setCreateMenuOpen(false);
                  onCreateEntity?.('Camera', 'camera');
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white text-left text-xs transition-colors"
              >
                <span>📷</span>
                <span>Camera Entity</span>
              </button>
            </div>
          )}

          <button title="Dock / Popout" className="p-1 hover:text-white hover:bg-slate-800 rounded">
            <DockIcon size={11} />
          </button>
          <button title="Collapse Hierarchy" className="p-1 hover:text-white hover:bg-slate-800 rounded">
            <CollapseIcon size={11} />
          </button>
        </div>
      </div>

      {/* Search Bar */}
      <div className="p-1.5 border-b border-[#19202c]">
        <div className="relative flex items-center">
          <SearchIcon size={11} className="absolute left-2 text-slate-500 pointer-events-none" />
          <input
            type="text"
            placeholder="Search..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="w-full bg-[#121822] text-slate-200 placeholder-slate-500 text-[11px] pl-6 pr-2 py-1 rounded border border-[#1e2738] focus:outline-none focus:border-indigo-500"
          />
        </div>
      </div>

      {/* Hierarchy Tree List */}
      <div className="flex-1 overflow-y-auto py-1 scrollbar-thin">
        {items.length === 0 ? (
          <div className="p-4 text-center text-xs text-slate-500 italic">
            No entities loaded in scene
          </div>
        ) : (
          items.map((node) => renderNode(node))
        )}
      </div>

      {/* Right Click Context Menu */}
      {contextMenu && (
        <div
          ref={contextMenuRef}
          style={{ top: `${contextMenu.y}px`, left: `${contextMenu.x}px` }}
          className="fixed w-44 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md text-xs select-none"
        >
          <div className="px-3 py-1 text-[10px] text-slate-400 font-mono border-b border-[#1f293d]">
            {contextMenu.item.name}
          </div>
          {contextMenu.item.entityId !== undefined && onDeleteEntity && (
            <button
              onClick={() => {
                onDeleteEntity(contextMenu.item.entityId!);
                setContextMenu(null);
              }}
              className="w-full px-3 py-1.5 flex items-center justify-between text-rose-400 hover:bg-rose-600 hover:text-white transition-colors text-left"
            >
              <span>Delete Entity</span>
              <span className="text-[10px] font-mono text-slate-400">Del</span>
            </button>
          )}
        </div>
      )}
    </div>
  );
};
