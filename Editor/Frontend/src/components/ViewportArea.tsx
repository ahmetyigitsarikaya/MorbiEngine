import React, { useRef, useState, useEffect } from 'react';
import {
  CubeIcon,
  ChevronDownIcon,
  MaximizeIcon,
  CameraIcon
} from './Icons';
import { AssetItem, GizmoTool } from '../types';
import { nativeBridge } from '../bridge/nativeBridge';

export type GizmoAxis = 'x' | 'y' | 'z' | 'xy' | 'xz' | 'yz' | 'uniform';

interface ViewportAreaProps {
  activeGizmoTool: GizmoTool;
  isDraggingGizmo: string | null;
  onGizmoMouseDown: (axis: GizmoAxis) => void;
  onGizmoMouseUp: () => void;
  onGizmoHandleDrag: (axis: GizmoAxis, step: number) => void;
  onViewportClick: (normX: number, normY: number) => void;
  onDropAsset?: (asset: AssetItem, normX: number, normY: number) => void;
  onImportExternalModel?: (path: string) => void;
  fps?: number;
  frameTimeMs?: number;
  drawCalls?: number;
  triangles?: string;
  memoryMb?: string;
}

export const ViewportArea: React.FC<ViewportAreaProps> = ({
  activeGizmoTool,
  isDraggingGizmo,
  onGizmoMouseDown,
  onGizmoMouseUp,
  onGizmoHandleDrag,
  onViewportClick,
  onDropAsset,
  onImportExternalModel,
  fps = 142,
  frameTimeMs = 7.04,
  drawCalls = 812,
  triangles = '1.6 M',
  memoryMb = '3.2 GB'
}) => {
  const [activeTab, setActiveTab] = useState<'Scene' | 'Game' | 'Asset Browser' | 'Visual Script' | 'Material Editor' | 'Build'>('Scene');
  const [viewShading, setViewShading] = useState<'Lit' | 'Wireframe' | 'Unlit'>('Lit');
  const [cameraProjection, setCameraProjection] = useState<'Perspective' | 'Orthographic'>('Perspective');
  const [coordSpace, setCoordSpace] = useState<'World' | 'Local'>('World');
  const [physicsDebug, setPhysicsDebug] = useState(false);
  const [isDragOver, setIsDragOver] = useState(false);
  const viewportRef = useRef<HTMLDivElement>(null);

  const dragInfoRef = useRef<{
    axis: GizmoAxis;
    lastX: number;
    lastY: number;
  } | null>(null);

  const startGizmoDrag = (axis: GizmoAxis, e: React.MouseEvent) => {
    e.stopPropagation();
    e.preventDefault();
    dragInfoRef.current = {
      axis,
      lastX: e.clientX,
      lastY: e.clientY
    };
    onGizmoMouseDown(axis);
  };

  useEffect(() => {
    const handleMouseMove = (e: MouseEvent) => {
      if (!dragInfoRef.current) return;
      const { axis, lastX, lastY } = dragInfoRef.current;
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      dragInfoRef.current.lastX = e.clientX;
      dragInfoRef.current.lastY = e.clientY;

      if (dx === 0 && dy === 0) return;

      if (activeGizmoTool === 'translate') {
        if (axis === 'x') {
          onGizmoHandleDrag('x', dx * 0.05);
        } else if (axis === 'y') {
          onGizmoHandleDrag('y', -dy * 0.05);
        } else if (axis === 'z') {
          onGizmoHandleDrag('z', (dx - dy) * 0.04);
        } else if (axis === 'xy') {
          onGizmoHandleDrag('xy', dx * 0.05);
        } else if (axis === 'xz') {
          onGizmoHandleDrag('xz', dx * 0.05);
        } else if (axis === 'yz') {
          onGizmoHandleDrag('yz', -dy * 0.05);
        }
      } else if (activeGizmoTool === 'rotate') {
        if (axis === 'x') {
          onGizmoHandleDrag('x', -dy * 0.8);
        } else if (axis === 'y') {
          onGizmoHandleDrag('y', dx * 0.8);
        } else if (axis === 'z') {
          onGizmoHandleDrag('z', (dx - dy) * 0.6);
        }
      } else if (activeGizmoTool === 'scale') {
        if (axis === 'uniform') {
          onGizmoHandleDrag('uniform', (dx - dy) * 0.02);
        } else if (axis === 'x') {
          onGizmoHandleDrag('x', dx * 0.02);
        } else if (axis === 'y') {
          onGizmoHandleDrag('y', -dy * 0.02);
        } else if (axis === 'z') {
          onGizmoHandleDrag('z', (dx - dy) * 0.02);
        }
      }
    };

    const handleMouseUp = () => {
      if (dragInfoRef.current) {
        dragInfoRef.current = null;
        onGizmoMouseUp();
      }
    };

    window.addEventListener('mousemove', handleMouseMove);
    window.addEventListener('mouseup', handleMouseUp);
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('mouseup', handleMouseUp);
    };
  }, [activeGizmoTool, onGizmoHandleDrag, onGizmoMouseUp]);

  const tabs: ('Scene' | 'Game' | 'Asset Browser' | 'Visual Script' | 'Material Editor' | 'Build')[] = [
    'Scene',
    'Game',
    'Asset Browser',
    'Visual Script',
    'Material Editor',
    'Build'
  ];

  // Dynamically sync real 3D Viewport layout rectangle to Native C++ Vulkan/DirectX pipeline
  useEffect(() => {
    if (!viewportRef.current) return;
    const ro = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const rect = entry.target.getBoundingClientRect();
        if (rect.width > 0 && rect.height > 0) {
          const dpr = window.devicePixelRatio || 1;
          nativeBridge.postJson({
            type: 'viewport_rect',
            x: Math.round(rect.left * dpr),
            y: Math.round(rect.top * dpr),
            width: Math.round(rect.width * dpr),
            height: Math.round(rect.height * dpr)
          });
        }
      }
    });
    ro.observe(viewportRef.current);
    return () => ro.disconnect();
  }, []);

  const handlePointerDown = (e: React.MouseEvent<HTMLDivElement>) => {
    if (!viewportRef.current) return;
    const rect = viewportRef.current.getBoundingClientRect();
    const normX = (e.clientX - rect.left) / rect.width;
    const normY = (e.clientY - rect.top) / rect.height;
    onViewportClick(normX, normY);
  };

  const handleTabSelect = (tab: typeof activeTab) => {
    setActiveTab(tab);
    nativeBridge.postJson({
      type: 'viewport_tab',
      tab
    });
  };

  return (
    <div className="flex-1 flex flex-col min-w-0 bg-transparent relative overflow-hidden select-none">
      {/* Top Tab Bar */}
      <div className="h-8 bg-[#0b0e14] border-b border-[#19202c] flex items-center px-2 gap-1 shrink-0 z-10">
        {tabs.map((tab) => {
          const isActive = activeTab === tab;
          return (
            <button
              key={tab}
              onClick={() => handleTabSelect(tab)}
              className={`flex items-center gap-1.5 px-3 py-1 rounded text-[11px] font-medium transition-all ${
                isActive
                  ? 'bg-indigo-600 text-white shadow-sm shadow-indigo-900/60'
                  : 'text-slate-400 hover:text-slate-200 hover:bg-[#151c27]'
              }`}
            >
              {tab === 'Scene' && <CubeIcon size={12} />}
              <span>{tab}</span>
            </button>
          );
        })}
      </div>

      {/* 3D Viewport Main Surface - 100% Transparent Cutout for Native Hardware-Accelerated 3D Scene */}
      <div
        ref={viewportRef}
        data-viewport-surface="true"
        onClick={handlePointerDown}
        onMouseUp={onGizmoMouseUp}
        onDragOver={(e) => {
          e.preventDefault();
          e.dataTransfer.dropEffect = 'copy';
          if (!isDragOver) setIsDragOver(true);
        }}
        onDragLeave={(e) => {
          if (e.currentTarget.contains(e.relatedTarget as Node)) return;
          setIsDragOver(false);
        }}
        onDrop={(e) => {
          e.preventDefault();
          setIsDragOver(false);
          try {
            // 1. External OS 3D model drop
            if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
              for (let i = 0; i < e.dataTransfer.files.length; i++) {
                const file = e.dataTransfer.files[i];
                const ext = file.name.split('.').pop()?.toLowerCase();
                if (['fbx', 'obj', 'gltf', 'glb', 'dae', 'stl', 'blend', '3ds', 'ply'].includes(ext || '')) {
                  const fullPath = (file as any).path || file.name;
                  onImportExternalModel?.(fullPath);
                  return;
                }
              }
            }

            // 2. Internal Asset Browser item drop
            let raw = e.dataTransfer.getData('application/json');
            if (!raw) {
              raw = e.dataTransfer.getData('text/plain');
            }
            if (raw && viewportRef.current) {
              const asset = JSON.parse(raw) as AssetItem;
              const rect = viewportRef.current.getBoundingClientRect();
              const normX = (e.clientX - rect.left) / rect.width;
              const normY = (e.clientY - rect.top) / rect.height;
              onDropAsset?.(asset, Math.max(0, Math.min(1, normX)), Math.max(0, Math.min(1, normY)));
            }
          } catch (err) {
            console.error('Failed to parse dropped asset', err);
          }
        }}
        className="flex-1 relative overflow-hidden cursor-crosshair flex items-center justify-center bg-transparent"
      >
        {/* Drag & Drop Visual Drop Zone Overlay */}
        {isDragOver && (
          <div className="absolute inset-0 border-2 border-indigo-500 bg-indigo-500/15 pointer-events-none z-30 flex items-center justify-center backdrop-blur-[1px] animate-pulse">
            <div className="bg-[#0b0f17]/90 border border-indigo-400 rounded-lg px-4 py-2.5 flex items-center gap-2.5 text-indigo-300 shadow-2xl shadow-indigo-950/90">
              <span className="text-lg">📦</span>
              <div className="flex flex-col">
                <span className="font-semibold text-xs text-white">Drop to Instantiate in 3D Scene</span>
                <span className="text-[10px] text-indigo-300/80 font-mono">Spawn Actor at Cursor Coordinates</span>
              </div>
            </div>
          </div>
        )}
        {/* Top-Left Floating Controls */}
        <div className="absolute top-2.5 left-2.5 flex items-center gap-1.5 z-10 pointer-events-auto">
          {/* Perspective Dropdown */}
          <div className="relative group">
            <button className="flex items-center gap-1.5 px-2 py-1 bg-[#101622]/85 hover:bg-[#182130] backdrop-blur-md border border-[#222d3e] rounded text-slate-200 text-[11px] shadow-sm">
              <CameraIcon size={12} className="text-sky-400" />
              <span>{cameraProjection}</span>
              <ChevronDownIcon size={10} className="text-slate-400" />
            </button>
            <div className="hidden group-hover:block absolute top-full left-0 mt-1 w-28 bg-[#101622] border border-[#222d3e] rounded shadow-lg py-1 text-[11px] text-slate-300">
              <div
                onClick={() => setCameraProjection('Perspective')}
                className="px-2.5 py-1 hover:bg-indigo-600 hover:text-white cursor-pointer"
              >
                Perspective
              </div>
              <div
                onClick={() => setCameraProjection('Orthographic')}
                className="px-2.5 py-1 hover:bg-indigo-600 hover:text-white cursor-pointer"
              >
                Orthographic
              </div>
            </div>
          </div>

          {/* Shading Mode Dropdown (Lit / Wireframe) */}
          <div className="relative group">
            <button className="flex items-center gap-1.5 px-2 py-1 bg-[#101622]/85 hover:bg-[#182130] backdrop-blur-md border border-[#222d3e] rounded text-slate-200 text-[11px] shadow-sm">
              <span className="w-2 h-2 rounded-full bg-emerald-400 inline-block" />
              <span>{viewShading}</span>
              <ChevronDownIcon size={10} className="text-slate-400" />
            </button>
            <div className="hidden group-hover:block absolute top-full left-0 mt-1 w-24 bg-[#101622] border border-[#222d3e] rounded shadow-lg py-1 text-[11px] text-slate-300">
              <div
                onClick={() => setViewShading('Lit')}
                className="px-2.5 py-1 hover:bg-indigo-600 hover:text-white cursor-pointer"
              >
                Lit
              </div>
              <div
                onClick={() => setViewShading('Wireframe')}
                className="px-2.5 py-1 hover:bg-indigo-600 hover:text-white cursor-pointer"
              >
                Wireframe
              </div>
              <div
                onClick={() => setViewShading('Unlit')}
                className="px-2.5 py-1 hover:bg-indigo-600 hover:text-white cursor-pointer"
              >
                Unlit
              </div>
            </div>
          </div>
        </div>

        {/* Top-Right Floating Controls & Performance Stats HUD */}
        <div className="absolute top-2.5 right-2.5 flex flex-col items-end gap-2 z-10 pointer-events-auto">
          {/* Mini Toolbar */}
          <div className="flex items-center gap-1 bg-[#101622]/85 backdrop-blur-md border border-[#222d3e] rounded p-0.5 text-slate-300 text-[11px]">
            {isDraggingGizmo && (
              <span className="px-1.5 py-0.5 rounded bg-indigo-900/80 border border-indigo-700/60 text-indigo-300 font-mono text-[9px] animate-pulse">
                AXIS [{isDraggingGizmo.toUpperCase()}]
              </span>
            )}
            <button
              onClick={() => setCoordSpace(coordSpace === 'World' ? 'Local' : 'World')}
              title="Coordinate Space (World / Local)"
              className="px-2 py-0.5 hover:bg-[#1c2637] rounded text-slate-200 font-mono text-[10px]"
            >
              {coordSpace}
            </button>
            <span className="w-[1px] h-3 bg-[#222d3e]" />
            <button
              onClick={() => {
                const next = !physicsDebug;
                setPhysicsDebug(next);
                nativeBridge.sendSetPhysicsDebugDraw(next);
              }}
              title="Toggle Jolt Physics Collider Wireframes"
              className={`px-1.5 py-0.5 rounded text-[10px] font-mono transition-colors ${
                physicsDebug
                  ? 'bg-emerald-700/80 text-white border border-emerald-500/60'
                  : 'hover:bg-[#1c2637] text-slate-300'
              }`}
            >
              Colliders
            </button>
            <span className="w-[1px] h-3 bg-[#222d3e]" />
            <button
              title="Grid Snapping: 0.5m"
              className="px-1.5 py-0.5 hover:bg-[#1c2637] rounded text-slate-300 text-[10px]"
            >
              0.5m
            </button>
            <span className="w-[1px] h-3 bg-[#222d3e]" />
            <button
              title="Aspect Ratio"
              className="px-1.5 py-0.5 hover:bg-[#1c2637] rounded text-slate-300 text-[10px] flex items-center gap-1"
            >
              16:9
              <ChevronDownIcon size={9} />
            </button>
            <span className="w-[1px] h-3 bg-[#222d3e]" />
            <button
              title="Maximize Viewport"
              className="p-1 hover:bg-[#1c2637] rounded text-slate-300"
            >
              <MaximizeIcon size={12} />
            </button>
          </div>

          {/* Floating Performance Stats HUD Card (Pixel-perfect to reference screenshot) */}
          <div className="bg-[#0b0f16]/85 backdrop-blur-md border border-[#1e2838] rounded-md px-3 py-2 text-[10px] font-mono text-slate-300 shadow-xl flex flex-col gap-1 w-36 pointer-events-none">
            <div className="flex items-center justify-between">
              <span className="text-slate-400">FPS:</span>
              <span className="text-emerald-400 font-semibold">{Math.round(fps)}</span>
            </div>
            <div className="flex items-center justify-between">
              <span className="text-slate-400">Frame Time:</span>
              <span className="text-slate-200">{frameTimeMs.toFixed(2)} ms</span>
            </div>
            <div className="flex items-center justify-between">
              <span className="text-slate-400">Draw Calls:</span>
              <span className="text-slate-200">{drawCalls}</span>
            </div>
            <div className="flex items-center justify-between">
              <span className="text-slate-400">Triangles:</span>
              <span className="text-slate-200">{triangles}</span>
            </div>
            <div className="flex items-center justify-between">
              <span className="text-slate-400">Memory:</span>
              <span className="text-slate-200">{memoryMb}</span>
            </div>
          </div>
        </div>

        {/* Center 3D Transform Gizmo (Interactive Overlay - Only in Scene Tab) */}
        {activeTab === 'Scene' && (activeGizmoTool === 'translate' || activeGizmoTool === 'rotate' || activeGizmoTool === 'scale') && (
          <div className="absolute inset-0 pointer-events-none flex items-center justify-center">
            <div className="relative w-52 h-52 pointer-events-auto">
              <svg className="w-full h-full" viewBox="-100 -100 200 200">
                {/* TRANSLATE GIZMO MODE */}
                {activeGizmoTool === 'translate' && (
                  <>
                    {/* Planar Quad Handles */}
                    {/* XY Plane (Yellow) */}
                    <rect
                      x="10"
                      y="-26"
                      width="16"
                      height="16"
                      fill="#eab308"
                      fillOpacity={isDraggingGizmo === 'xy' ? '0.7' : '0.25'}
                      stroke="#eab308"
                      strokeWidth="1.5"
                      className="cursor-move hover:fill-opacity-70 transition-all"
                      onMouseDown={(e) => startGizmoDrag('xy', e)}
                    />
                    {/* XZ Plane (Pink) */}
                    <polygon
                      points="12,0 28,0 12,16 0,16"
                      fill="#ec4899"
                      fillOpacity={isDraggingGizmo === 'xz' ? '0.7' : '0.25'}
                      stroke="#ec4899"
                      strokeWidth="1.5"
                      className="cursor-move hover:fill-opacity-70 transition-all"
                      onMouseDown={(e) => startGizmoDrag('xz', e)}
                    />
                    {/* YZ Plane (Cyan) */}
                    <polygon
                      points="0,-12 0,-28 -12,-16 -12,0"
                      fill="#06b6d4"
                      fillOpacity={isDraggingGizmo === 'yz' ? '0.7' : '0.25'}
                      stroke="#06b6d4"
                      strokeWidth="1.5"
                      className="cursor-move hover:fill-opacity-70 transition-all"
                      onMouseDown={(e) => startGizmoDrag('yz', e)}
                    />

                    {/* X Axis (Red) */}
                    <line x1="0" y1="0" x2="65" y2="0" stroke="transparent" strokeWidth="18" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)} />
                    <line x1="0" y1="0" x2="62" y2="0" stroke="#ef4444" strokeWidth={isDraggingGizmo === 'x' ? 3.5 : 2.5} className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)} />
                    <polygon
                      points="62,-5 76,0 62,5"
                      fill="#ef4444"
                      className="cursor-pointer hover:opacity-80"
                      onMouseDown={(e) => startGizmoDrag('x', e)}
                    />
                    <text x="80" y="4" fill="#ef4444" fontSize="11" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)}>X</text>

                    {/* Y Axis (Green, Upward) */}
                    <line x1="0" y1="0" x2="0" y2="-65" stroke="transparent" strokeWidth="18" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)} />
                    <line x1="0" y1="0" x2="0" y2="-62" stroke="#22c55e" strokeWidth={isDraggingGizmo === 'y' ? 3.5 : 2.5} className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)} />
                    <polygon
                      points="-5,-62 0,-76 5,-62"
                      fill="#22c55e"
                      className="cursor-pointer hover:opacity-80"
                      onMouseDown={(e) => startGizmoDrag('y', e)}
                    />
                    <text x="3" y="-80" fill="#22c55e" fontSize="11" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)}>Y</text>

                    {/* Z Axis (Blue, Depth) */}
                    <line x1="0" y1="0" x2="-46" y2="46" stroke="transparent" strokeWidth="18" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)} />
                    <line x1="0" y1="0" x2="-44" y2="44" stroke="#3b82f6" strokeWidth={isDraggingGizmo === 'z' ? 3.5 : 2.5} className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)} />
                    <polygon
                      points="-40,36 -56,56 -36,40"
                      fill="#3b82f6"
                      className="cursor-pointer hover:opacity-80"
                      onMouseDown={(e) => startGizmoDrag('z', e)}
                    />
                    <text x="-66" y="62" fill="#3b82f6" fontSize="11" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)}>Z</text>

                    {/* Center Pivot Point */}
                    <circle cx="0" cy="0" r="5" fill="#38bdf8" />
                  </>
                )}

                {/* ROTATE GIZMO MODE */}
                {activeGizmoTool === 'rotate' && (
                  <>
                    {/* Pitch (X Axis Ring - Red) */}
                    <circle cx="0" cy="0" r="54" fill="none" stroke="transparent" strokeWidth="16" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)} />
                    <circle
                      cx="0"
                      cy="0"
                      r="54"
                      fill="none"
                      stroke={isDraggingGizmo === 'x' ? '#f87171' : '#ef4444'}
                      strokeWidth={isDraggingGizmo === 'x' ? 4 : 2.5}
                      strokeDasharray="6 3"
                      className="cursor-pointer hover:stroke-white transition-colors"
                      onMouseDown={(e) => startGizmoDrag('x', e)}
                    />
                    <circle cx="-54" cy="0" r="5.5" fill="#ef4444" stroke="#ffffff" strokeWidth="1.5" className="cursor-pointer hover:scale-125 transition-transform" onMouseDown={(e) => startGizmoDrag('x', e)} />
                    <text x="-76" y="4" fill="#ef4444" fontSize="10" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)}>Rx</text>

                    {/* Yaw (Y Axis Ring - Green) */}
                    <ellipse cx="0" cy="0" rx="54" ry="24" fill="none" stroke="transparent" strokeWidth="16" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)} />
                    <ellipse
                      cx="0"
                      cy="0"
                      rx="54"
                      ry="24"
                      fill="none"
                      stroke={isDraggingGizmo === 'y' ? '#4ade80' : '#22c55e'}
                      strokeWidth={isDraggingGizmo === 'y' ? 4 : 2.5}
                      className="cursor-pointer hover:stroke-white transition-colors"
                      onMouseDown={(e) => startGizmoDrag('y', e)}
                    />
                    <circle cx="54" cy="0" r="5.5" fill="#22c55e" stroke="#ffffff" strokeWidth="1.5" className="cursor-pointer hover:scale-125 transition-transform" onMouseDown={(e) => startGizmoDrag('y', e)} />
                    <text x="64" y="4" fill="#22c55e" fontSize="10" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)}>Ry</text>

                    {/* Roll (Z Axis Ring - Blue) */}
                    <ellipse cx="0" cy="0" rx="24" ry="54" fill="none" stroke="transparent" strokeWidth="16" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)} />
                    <ellipse
                      cx="0"
                      cy="0"
                      rx="24"
                      ry="54"
                      fill="none"
                      stroke={isDraggingGizmo === 'z' ? '#60a5fa' : '#3b82f6'}
                      strokeWidth={isDraggingGizmo === 'z' ? 4 : 2.5}
                      className="cursor-pointer hover:stroke-white transition-colors"
                      onMouseDown={(e) => startGizmoDrag('z', e)}
                    />
                    <circle cx="0" cy="-54" r="5.5" fill="#3b82f6" stroke="#ffffff" strokeWidth="1.5" className="cursor-pointer hover:scale-125 transition-transform" onMouseDown={(e) => startGizmoDrag('z', e)} />
                    <text x="7" y="-56" fill="#3b82f6" fontSize="10" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)}>Rz</text>

                    {/* Center Point */}
                    <circle cx="0" cy="0" r="4.5" fill="#38bdf8" />
                  </>
                )}

                {/* SCALE GIZMO MODE */}
                {activeGizmoTool === 'scale' && (
                  <>
                    {/* X Axis (Red) */}
                    <line x1="0" y1="0" x2="68" y2="0" stroke="transparent" strokeWidth="18" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)} />
                    <line x1="0" y1="0" x2="58" y2="0" stroke="#ef4444" strokeWidth={isDraggingGizmo === 'x' ? 3.5 : 2.5} className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)} />
                    <rect
                      x="58"
                      y="-5"
                      width="10"
                      height="10"
                      fill="#ef4444"
                      stroke="#ffffff"
                      strokeWidth="1"
                      className="cursor-pointer hover:scale-125 transition-transform"
                      onMouseDown={(e) => startGizmoDrag('x', e)}
                    />
                    <text x="72" y="4" fill="#ef4444" fontSize="10" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('x', e)}>Sx</text>

                    {/* Y Axis (Green) */}
                    <line x1="0" y1="0" x2="0" y2="-68" stroke="transparent" strokeWidth="18" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)} />
                    <line x1="0" y1="0" x2="0" y2="-58" stroke="#22c55e" strokeWidth={isDraggingGizmo === 'y' ? 3.5 : 2.5} className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)} />
                    <rect
                      x="-5"
                      y="-68"
                      width="10"
                      height="10"
                      fill="#22c55e"
                      stroke="#ffffff"
                      strokeWidth="1"
                      className="cursor-pointer hover:scale-125 transition-transform"
                      onMouseDown={(e) => startGizmoDrag('y', e)}
                    />
                    <text x="3" y="-72" fill="#22c55e" fontSize="10" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('y', e)}>Sy</text>

                    {/* Z Axis (Blue) */}
                    <line x1="0" y1="0" x2="-48" y2="48" stroke="transparent" strokeWidth="18" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)} />
                    <line x1="0" y1="0" x2="-42" y2="42" stroke="#3b82f6" strokeWidth={isDraggingGizmo === 'z' ? 3.5 : 2.5} className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)} />
                    <rect
                      x="-50"
                      y="42"
                      width="10"
                      height="10"
                      fill="#3b82f6"
                      stroke="#ffffff"
                      strokeWidth="1"
                      className="cursor-pointer hover:scale-125 transition-transform"
                      onMouseDown={(e) => startGizmoDrag('z', e)}
                    />
                    <text x="-66" y="58" fill="#3b82f6" fontSize="10" fontWeight="bold" className="cursor-pointer" onMouseDown={(e) => startGizmoDrag('z', e)}>Sz</text>

                    {/* Center Uniform Scale Cube (Gold) */}
                    <rect
                      x="-7"
                      y="-7"
                      width="14"
                      height="14"
                      fill="#fbbf24"
                      stroke="#d97706"
                      strokeWidth="1.5"
                      className="cursor-pointer hover:scale-125 transition-transform"
                      onMouseDown={(e) => startGizmoDrag('uniform', e)}
                    >
                      <title>Uniform 3D Scale (Drag for all axes)</title>
                    </rect>
                  </>
                )}
              </svg>
            </div>
          </div>
        )}

        {/* Bottom-Left 3D Orientation Coordinate Tripod */}
        <div className="absolute bottom-3 left-3 flex items-center gap-1 pointer-events-none">
          <svg width="40" height="40" viewBox="-20 -20 40 40">
            <line x1="0" y1="0" x2="16" y2="0" stroke="#ef4444" strokeWidth="2" />
            <text x="18" y="3" fill="#ef4444" fontSize="8" fontWeight="bold">X</text>
            <line x1="0" y1="0" x2="0" y2="-16" stroke="#22c55e" strokeWidth="2" />
            <text x="1" y="-18" fill="#22c55e" fontSize="8" fontWeight="bold">Y</text>
            <line x1="0" y1="0" x2="-12" y2="12" stroke="#3b82f6" strokeWidth="2" />
            <text x="-18" y="18" fill="#3b82f6" fontSize="8" fontWeight="bold">Z</text>
          </svg>
        </div>
      </div>
    </div>
  );
};
