import React, { useState, useRef, useMemo } from 'react';
import {
  SelectGizmoIcon,
  PinIcon,
  EyeIcon,
  LayersIcon
} from './Icons';
import { HierarchyItem } from '../types';

interface MiniMapPanelProps {
  playerPos?: [number, number, number];
  playerRot?: [number, number, number];
  entities?: HierarchyItem[];
  selectedEntityId?: number;
  onSelectEntity?: (entity: HierarchyItem) => void;
  onTeleportPlayer?: (x: number, z: number) => void;
  playMode?: 'edit' | 'playing' | 'paused';
}

export const MiniMapPanel: React.FC<MiniMapPanelProps> = ({
  playerPos = [0, 0, 0],
  playerRot = [0, 0, 0],
  entities = [],
  selectedEntityId,
  onSelectEntity,
  onTeleportPlayer,
  playMode = 'edit'
}) => {
  const [activeTab, setActiveTab] = useState<'Scene View' | 'Game View'>('Scene View');

  // MiniMap Camera State
  const [zoom, setZoom] = useState<number>(1.2);
  const [pan, setPan] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const [isPanning, setIsPanning] = useState(false);
  const [dragStart, setDragStart] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const [autoFollow, setAutoFollow] = useState(true);
  const [showLabels, setShowLabels] = useState(true);
  const [showGrid, setShowGrid] = useState(true);
  const [showBackdrop, setShowBackdrop] = useState(true);
  const [hoverWorldCoord, setHoverWorldCoord] = useState<{ x: number; z: number } | null>(null);

  const containerRef = useRef<HTMLDivElement>(null);
  const mapWidth = 310 - 28; // Minus left toolbar width (28px)
  const mapHeight = 248; // Total panel height 280 minus tab header 32
  const centerScreen = { x: mapWidth / 2, y: mapHeight / 2 };
  const meterToPx = 4.5; // Base scale: 1 meter = 4.5px at 1.0x zoom

  // World (X, Z) to Map Screen (X, Y) Coordinates
  const worldToScreen = (worldX: number, worldZ: number) => {
    const scale = meterToPx * zoom;
    const offsetX = autoFollow ? -playerPos[0] * scale : pan.x;
    const offsetY = autoFollow ? playerPos[2] * scale : pan.y;

    const screenX = centerScreen.x + offsetX + worldX * scale;
    const screenY = centerScreen.y + offsetY - worldZ * scale; // Inverted Z because forward is up
    return { x: screenX, y: screenY };
  };

  // Map Screen (X, Y) to World (X, Z) Coordinates
  const screenToWorld = (screenX: number, screenY: number) => {
    const scale = meterToPx * zoom;
    const offsetX = autoFollow ? -playerPos[0] * scale : pan.x;
    const offsetY = autoFollow ? playerPos[2] * scale : pan.y;

    const worldX = (screenX - centerScreen.x - offsetX) / scale;
    const worldZ = -(screenY - centerScreen.y - offsetY) / scale;
    return { x: worldX, z: worldZ };
  };

  // Mouse pan handlers
  const handleMouseDown = (e: React.MouseEvent<HTMLDivElement>) => {
    if (e.button === 0 || e.button === 1) { // Left or middle click
      setIsPanning(true);
      setDragStart({ x: e.clientX - pan.x, y: e.clientY - pan.y });
      if (autoFollow) setAutoFollow(false);
    }
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLDivElement>) => {
    if (isPanning) {
      setPan({
        x: e.clientX - dragStart.x,
        y: e.clientY - dragStart.y
      });
    }

    if (containerRef.current) {
      const rect = containerRef.current.getBoundingClientRect();
      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top;
      const world = screenToWorld(mouseX, mouseY);
      setHoverWorldCoord({ x: world.x, z: world.z });
    }
  };

  const handleMouseUp = () => {
    setIsPanning(false);
  };

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    e.preventDefault();
    const zoomFactor = e.deltaY < 0 ? 1.15 : 0.85;
    setZoom((prev) => Math.min(3.5, Math.max(0.4, Number((prev * zoomFactor).toFixed(2)))));
  };

  // Click on map to teleport or move player / selected entity
  const handleMapClick = (e: React.MouseEvent<HTMLDivElement>) => {
    // Only trigger if not dragged
    if (Math.abs(e.movementX) > 2 || Math.abs(e.movementY) > 2) return;
    if (containerRef.current) {
      const rect = containerRef.current.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      const clickY = e.clientY - rect.top;
      const world = screenToWorld(clickX, clickY);

      if (onTeleportPlayer) {
        onTeleportPlayer(world.x, world.z);
      }
    }
  };

  // Center on player
  const handleCenterOnPlayer = () => {
    setAutoFollow(true);
    setPan({ x: 0, y: 0 });
    setZoom(1.2);
  };

  // Filter and compute screen positions of alive scene entities
  const activeEntityMarkers = useMemo(() => {
    return entities.map((ent) => {
      const pos = ent.componentData?.transform?.position || [0, 0, 0];
      const screen = worldToScreen(pos[0], pos[2]);
      const isSelected = selectedEntityId !== undefined && ent.entityId === selectedEntityId;
      const isPlayer = ent.name.toLowerCase().includes('remy') || ent.name.toLowerCase().includes('player');
      return {
        item: ent,
        worldPos: pos,
        screenX: screen.x,
        screenY: screen.y,
        isSelected,
        isPlayer
      };
    });
  }, [entities, playerPos, pan, zoom, autoFollow, selectedEntityId]);

  const playerScreen = worldToScreen(playerPos[0], playerPos[2]);
  const yawAngle = playerRot[1] || 0;

  return (
    <div className="w-[310px] h-[280px] bg-[#0c1017] border-t border-[#19202c] flex flex-col select-none shrink-0 overflow-hidden relative">
      {/* 1. Header Tabs & Quick Actions */}
      <div className="h-8 px-2 bg-[#090c12] border-b border-[#19202c] flex items-center justify-between text-[11px] shrink-0 z-20">
        <div className="flex items-center gap-1">
          <button
            onClick={() => setActiveTab('Scene View')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors font-medium ${
              activeTab === 'Scene View'
                ? 'bg-indigo-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <span>Scene View</span>
          </button>
          <button
            onClick={() => setActiveTab('Game View')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors font-medium ${
              activeTab === 'Game View'
                ? 'bg-indigo-600 text-white shadow-sm'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <span>Game View</span>
          </button>
        </div>

        {/* Live Simulation Indicator Badge */}
        <div className="flex items-center gap-1 text-[10px]">
          <span
            className={`w-1.5 h-1.5 rounded-full ${
              playMode === 'playing' ? 'bg-emerald-400 animate-pulse' : 'bg-slate-500'
            }`}
          />
          <span className="font-mono text-slate-400">
            {playMode === 'playing' ? 'PIE' : 'EDIT'}
          </span>
        </div>
      </div>

      {/* 2. Main Surface with Left Mini-Tools */}
      <div className="flex-1 flex overflow-hidden relative bg-[#0a0d14]">
        {/* Left Mini-Dock Toolbar */}
        <div className="w-7 bg-[#090c12] border-r border-[#19202c] flex flex-col items-center py-2 gap-2.5 text-slate-400 shrink-0 z-20">
          {/* Tool 1: Center on Player / Focus */}
          <button
            title="Focus & Center on Player"
            onClick={handleCenterOnPlayer}
            className={`p-1 rounded transition-all ${
              autoFollow ? 'text-indigo-400 bg-indigo-950/60 border border-indigo-700/50' : 'hover:text-white hover:bg-slate-800'
            }`}
          >
            <SelectGizmoIcon size={12} />
          </button>

          {/* Tool 2: Pin / Auto-Follow */}
          <button
            title="Lock Auto-Follow Camera"
            onClick={() => setAutoFollow((prev) => !prev)}
            className={`p-1 rounded transition-all ${
              autoFollow ? 'text-emerald-400 bg-emerald-950/60 border border-emerald-700/50' : 'hover:text-white hover:bg-slate-800'
            }`}
          >
            <PinIcon size={12} />
          </button>

          {/* Tool 3: Toggle Entity Labels */}
          <button
            title="Toggle Entity Labels"
            onClick={() => setShowLabels((prev) => !prev)}
            className={`p-1 rounded transition-all ${
              showLabels ? 'text-sky-400 bg-sky-950/60 border border-sky-700/50' : 'hover:text-white hover:bg-slate-800'
            }`}
          >
            <EyeIcon size={12} />
          </button>

          {/* Tool 4: Toggle Grid & Distance Rings */}
          <button
            title="Toggle Metric Grid & Rings"
            onClick={() => setShowGrid((prev) => !prev)}
            className={`p-1 rounded transition-all ${
              showGrid ? 'text-amber-400 bg-amber-950/60 border border-amber-700/50' : 'hover:text-white hover:bg-slate-800'
            }`}
          >
            <LayersIcon size={12} />
          </button>

          {/* Tool 5: Toggle Satellite Terrain Backdrop */}
          <button
            title="Toggle Satellite Backdrop"
            onClick={() => setShowBackdrop((prev) => !prev)}
            className={`p-1 rounded transition-all text-[9px] font-mono ${
              showBackdrop ? 'text-teal-400 bg-teal-950/60 border border-teal-700/50' : 'hover:text-white hover:bg-slate-800'
            }`}
          >
            SAT
          </button>

          {/* Tool 6: Reset Zoom */}
          <button
            title="Reset Zoom to 1.0x"
            onClick={() => setZoom(1.0)}
            className="text-[9px] font-mono p-1 rounded hover:text-white hover:bg-slate-800"
          >
            1x
          </button>
        </div>

        {/* Tactical Map Container */}
        {activeTab === 'Scene View' ? (
          <div
            ref={containerRef}
            onMouseDown={handleMouseDown}
            onMouseMove={handleMouseMove}
            onMouseUp={handleMouseUp}
            onMouseLeave={handleMouseUp}
            onWheel={handleWheel}
            onClick={handleMapClick}
            className="flex-1 relative overflow-hidden bg-[#090d14] cursor-crosshair select-none"
          >
            {/* Satellite / Topographic Terrain Backdrop */}
            {showBackdrop && (
              <img
                src="./minimap.jpg"
                alt="Tactical Top-Down Terrain Map"
                className="absolute inset-0 w-full h-full object-cover select-none pointer-events-none opacity-35 filter brightness-75 contrast-125"
              />
            )}

            {/* Interactive SVG Radar & Entity Overlay */}
            <svg className="absolute inset-0 w-full h-full pointer-events-none">
              <defs>
                {/* Radar Grid Pattern */}
                <pattern id="radarGrid" width={25 * zoom} height={25 * zoom} patternUnits="userSpaceOnUse">
                  <path
                    d={`M ${25 * zoom} 0 L 0 0 0 ${25 * zoom}`}
                    fill="none"
                    stroke="rgba(99, 102, 241, 0.12)"
                    strokeWidth="1"
                  />
                </pattern>
                {/* Player FOV Gradient */}
                <radialGradient id="fovConeGrad" cx="50%" cy="100%" r="90%">
                  <stop offset="0%" stopColor="rgba(99, 102, 241, 0.45)" />
                  <stop offset="70%" stopColor="rgba(99, 102, 241, 0.15)" />
                  <stop offset="100%" stopColor="rgba(99, 102, 241, 0.0)" />
                </radialGradient>
              </defs>

              {/* Background Grid */}
              {showGrid && <rect width="100%" height="100%" fill="url(#radarGrid)" />}

              {/* World Axis Crosshair (X=0, Z=0) */}
              {showGrid && (
                <g>
                  {(() => {
                    const origin = worldToScreen(0, 0);
                    return (
                      <>
                        {/* Red X Axis Line */}
                        <line
                          x1="0"
                          y1={origin.y}
                          x2={mapWidth}
                          y2={origin.y}
                          stroke="rgba(239, 68, 68, 0.25)"
                          strokeWidth="1"
                          strokeDasharray="4 4"
                        />
                        {/* Blue Z Axis Line */}
                        <line
                          x1={origin.x}
                          y1="0"
                          x2={origin.x}
                          y2={mapHeight}
                          stroke="rgba(59, 130, 246, 0.25)"
                          strokeWidth="1"
                          strokeDasharray="4 4"
                        />
                        {/* Origin Marker */}
                        <circle cx={origin.x} cy={origin.y} r="3" fill="#64748b" opacity="0.6" />
                      </>
                    );
                  })()}
                </g>
              )}

              {/* Concentric Distance Rings around Player (10m, 25m, 50m) */}
              {showGrid && (
                <g opacity="0.3">
                  <circle
                    cx={playerScreen.x}
                    cy={playerScreen.y}
                    r={10 * meterToPx * zoom}
                    fill="none"
                    stroke="#818cf8"
                    strokeWidth="1"
                    strokeDasharray="3 3"
                  />
                  <circle
                    cx={playerScreen.x}
                    cy={playerScreen.y}
                    r={25 * meterToPx * zoom}
                    fill="none"
                    stroke="#818cf8"
                    strokeWidth="1"
                    strokeDasharray="5 5"
                  />
                </g>
              )}

              {/* Scene Entity Markers */}
              {activeEntityMarkers.map((ent, idx) => {
                if (ent.isPlayer) return null; // Player is drawn separately with full FOV
                const isSelected = ent.isSelected;
                const iconColor = ent.item.iconType === 'camera'
                  ? '#38bdf8'
                  : ent.item.iconType === 'light'
                  ? '#facc15'
                  : '#a78bfa';

                return (
                  <g
                    key={ent.item.id || idx}
                    className="cursor-pointer pointer-events-auto"
                    onClick={(e) => {
                      e.stopPropagation();
                      if (onSelectEntity) onSelectEntity(ent.item);
                    }}
                  >
                    {/* Pulsing Selection Ring */}
                    {isSelected && (
                      <circle
                        cx={ent.screenX}
                        cy={ent.screenY}
                        r="12"
                        fill="none"
                        stroke="#818cf8"
                        strokeWidth="1.5"
                        strokeDasharray="4 2"
                        className="animate-spin"
                      />
                    )}
                    {/* Entity Dot */}
                    <circle
                      cx={ent.screenX}
                      cy={ent.screenY}
                      r="4.5"
                      fill={iconColor}
                      stroke="#090d14"
                      strokeWidth="1.5"
                    />
                    {/* Entity Label */}
                    {showLabels && (
                      <text
                        x={ent.screenX + 7}
                        y={ent.screenY + 3}
                        fontSize="9"
                        fill="#cbd5e1"
                        fontFamily="monospace"
                        className="select-none"
                      >
                        {ent.item.name}
                      </text>
                    )}
                  </g>
                );
              })}

              {/* LIVE PLAYER WAYPOINT MARKER (White Triangle with Yaw Rotation) */}
              <g
                transform={`translate(${playerScreen.x}, ${playerScreen.y}) rotate(${yawAngle})`}
                className="pointer-events-none"
              >
                {/* Translucent FOV Vision Cone */}
                <path
                  d="M 0 0 L -24 -46 A 52 52 0 0 1 24 -46 Z"
                  fill="url(#fovConeGrad)"
                  stroke="rgba(129, 140, 248, 0.45)"
                  strokeWidth="1"
                />

                {/* Outer Glow Halo */}
                <circle
                  cx="0"
                  cy="0"
                  r="13"
                  fill="rgba(99, 102, 241, 0.2)"
                  stroke="rgba(255, 255, 255, 0.85)"
                  strokeWidth="1.5"
                />

                {/* White Directional Triangle */}
                <polygon
                  points="0,-9 6,6 -6,6"
                  fill="#ffffff"
                  stroke="#4f46e5"
                  strokeWidth="1"
                />
              </g>

              {/* Player Label */}
              {showLabels && (
                <text
                  x={playerScreen.x + 14}
                  y={playerScreen.y - 8}
                  fontSize="9.5"
                  fontWeight="bold"
                  fill="#38bdf8"
                  fontFamily="monospace"
                  className="pointer-events-none select-none"
                >
                  Remy [Active]
                </text>
              )}
            </svg>

            {/* Top-Right Live Player World Coordinates Badge */}
            <div className="absolute top-1.5 right-1.5 px-2 py-0.5 rounded bg-black/75 backdrop-blur-md border border-white/10 text-[9px] font-mono text-slate-200 flex items-center gap-1.5 shadow-md">
              <span className="text-red-400 font-bold">X:</span>
              <span>{playerPos[0].toFixed(1)}</span>
              <span className="text-slate-500">|</span>
              <span className="text-blue-400 font-bold">Z:</span>
              <span>{playerPos[2].toFixed(1)}</span>
              <span className="text-slate-500">|</span>
              <span className="text-emerald-400 font-bold">Y:</span>
              <span>{playerPos[1].toFixed(1)}m</span>
            </div>

            {/* Bottom-Right Zoom HUD Buttons */}
            <div className="absolute bottom-1.5 right-1.5 flex items-center gap-1 bg-black/70 backdrop-blur-sm border border-[#1e2738] rounded px-1.5 py-0.5 text-[9px] text-slate-300">
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  setZoom((prev) => Math.max(0.4, Number((prev - 0.2).toFixed(2))));
                }}
                className="w-4 h-4 flex items-center justify-center hover:bg-slate-800 rounded font-bold"
                title="Zoom Out"
              >
                -
              </button>
              <span className="font-mono text-[8.5px] px-0.5">
                {(zoom * 100).toFixed(0)}%
              </span>
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  setZoom((prev) => Math.min(3.5, Number((prev + 0.2).toFixed(2))));
                }}
                className="w-4 h-4 flex items-center justify-center hover:bg-slate-800 rounded font-bold"
                title="Zoom In"
              >
                +
              </button>
            </div>

            {/* Bottom-Left Hover Coordinates */}
            {hoverWorldCoord && (
              <div className="absolute bottom-1.5 left-1.5 text-[8.5px] font-mono text-slate-400 bg-black/60 px-1 rounded pointer-events-none">
                Cursor: ({hoverWorldCoord.x.toFixed(1)}, {hoverWorldCoord.z.toFixed(1)})
              </div>
            )}
          </div>
        ) : (
          /* GAME VIEW: Live Tactical Camera & Simulation Preview */
          <div className="flex-1 relative overflow-hidden bg-[#070a10] flex flex-col items-center justify-center p-3 select-none">
            {/* Tactical Grid Background */}
            <div className="absolute inset-0 opacity-15 bg-[linear-gradient(to_right,#6366f1_1px,transparent_1px),linear-gradient(to_bottom,#6366f1_1px,transparent_1px)] bg-[size:16px_16px]" />

            {/* Tactical HUD Reticle */}
            <div className="relative w-32 h-32 rounded-full border border-indigo-500/30 flex items-center justify-center">
              <div className="w-16 h-16 rounded-full border border-indigo-400/50 flex items-center justify-center animate-pulse">
                <div className="w-2 h-2 rounded-full bg-emerald-400" />
              </div>
              {/* Crosshair ticks */}
              <div className="absolute top-0 w-0.5 h-3 bg-indigo-400/80" />
              <div className="absolute bottom-0 w-0.5 h-3 bg-indigo-400/80" />
              <div className="absolute left-0 h-0.5 w-3 bg-indigo-400/80" />
              <div className="absolute right-0 h-0.5 w-3 bg-indigo-400/80" />
            </div>

            {/* Game Simulation Readouts */}
            <div className="mt-3 text-center space-y-1 z-10">
              <div className="text-[11px] font-semibold text-slate-200">
                {playMode === 'playing' ? 'PIE Gameplay Camera Active' : 'Camera Standby (Press Play)'}
              </div>
              <div className="text-[9.5px] font-mono text-slate-400 flex items-center justify-center gap-2">
                <span>Yaw: {playerRot[1].toFixed(1)}°</span>
                <span>•</span>
                <span>Alt: {playerPos[1].toFixed(2)}m</span>
              </div>
            </div>

            {/* Quick Actions */}
            <div className="mt-2.5 flex items-center gap-1.5 z-10">
              <button
                onClick={() => {
                  if (onTeleportPlayer) onTeleportPlayer(0, 0);
                }}
                className="px-2 py-0.5 rounded bg-indigo-950/80 hover:bg-indigo-700/60 border border-indigo-600/50 text-[9.5px] text-indigo-300 transition-colors"
              >
                Reset to Origin
              </button>
              <button
                onClick={handleCenterOnPlayer}
                className="px-2 py-0.5 rounded bg-slate-800 hover:bg-slate-700 text-[9.5px] text-slate-300 transition-colors"
              >
                Follow Actor
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
