import React, { useState } from 'react';
import {
  CubeIcon,
  ChevronDownIcon,
  ChevronRightIcon,
  PlusIcon,
  DockIcon
} from './Icons';
import { HierarchyItem } from '../types';
import { nativeBridge } from '../bridge/nativeBridge';

interface InspectorPanelProps {
  selectedEntity: HierarchyItem;
  position: [number, number, number];
  rotation: [number, number, number];
  scale: [number, number, number];
  onPositionChange: (axis: 0 | 1 | 2, val: number) => void;
  onRotationChange: (axis: 0 | 1 | 2, val: number) => void;
  onScaleChange: (axis: 0 | 1 | 2, val: number) => void;
  onRenameEntity?: (entityId: number, name: string) => void;
  onAddComponent?: (entityId: number, componentType: string) => void;
}

export const InspectorPanel: React.FC<InspectorPanelProps> = ({
  selectedEntity,
  position,
  rotation,
  scale,
  onPositionChange,
  onRotationChange,
  onScaleChange,
  onRenameEntity,
  onAddComponent
}) => {
  const [activeTab, setActiveTab] = useState<'Inspector' | 'Outliner'>('Inspector');
  const [isEntityActive, setIsEntityActive] = useState(true);
  const [entityName, setEntityName] = useState(selectedEntity.name);
  const [addComponentOpen, setAddComponentOpen] = useState(false);
  const addCompRef = React.useRef<HTMLDivElement>(null);
  const addCompDropdownRef = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    setEntityName(selectedEntity.name);
  }, [selectedEntity.name]);

  React.useEffect(() => {
    if (!addComponentOpen) return;

    const handleClickOutside = (e: MouseEvent) => {
      if (addCompRef.current && !addCompRef.current.contains(e.target as Node)) {
        setAddComponentOpen(false);
      }
    };
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setAddComponentOpen(false);
      }
    };
    window.addEventListener('pointerdown', handleClickOutside);
    window.addEventListener('keydown', handleKeyDown);
    return () => {
      window.removeEventListener('pointerdown', handleClickOutside);
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, [addComponentOpen]);

  React.useLayoutEffect(() => {
    if (!addComponentOpen || !addCompDropdownRef.current) {
      nativeBridge.sendMenuState(false, undefined, 'inspector');
      return;
    }

    const updateRect = () => {
      if (!addCompDropdownRef.current) return;
      const rect = addCompDropdownRef.current.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      nativeBridge.sendMenuState(true, {
        x: Math.max(0, Math.round((rect.left - 4) * dpr)),
        y: Math.max(0, Math.round((rect.top - 4) * dpr)),
        width: Math.max(0, Math.round((rect.width + 8) * dpr)),
        height: Math.max(0, Math.round((rect.height + 8) * dpr))
      }, 'inspector');
    };

    updateRect();
    window.addEventListener('resize', updateRect);
    return () => {
      window.removeEventListener('resize', updateRect);
      nativeBridge.sendMenuState(false, undefined, 'inspector');
    };
  }, [addComponentOpen]);

  const handleNameCommit = () => {
    const trimmed = entityName.trim();
    if (trimmed && trimmed !== selectedEntity.name && selectedEntity.entityId !== undefined) {
      onRenameEntity?.(selectedEntity.entityId, trimmed);
    }
  };

  // Collapsible component card states
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({
    transform: false,
    meshRenderer: false,
    animator: false,
    rigidbody: false,
    compoundColliders: false,
    physicsJoints: false,
    characterController: false,
    script: false,
    light: false,
    camera: false
  });

  const toggleComponent = (name: string) => {
    setCollapsed((prev) => ({ ...prev, [name]: !prev[name] }));
  };

  const compData = selectedEntity.componentData;
  const tag = compData?.tag || 'Default';
  const layer = compData?.layer || 'Default';
  const meshComp = compData?.meshRenderer;
  const animComp = compData?.animator;
  const rbComp = compData?.rigidbody;
  const compoundList = compData?.compoundColliders || [];
  const jointList = compData?.physicsJoints || [];
  const charComp = compData?.characterController;
  const scriptComp = compData?.script;
  const lightComp = compData?.light;
  const cameraComp = compData?.camera;

  const commitPhysicsPatch = (
    kind: 'rigidbody' | 'characterController' | 'compoundColliders' | 'physicsJoints',
    patch: Record<string, unknown>
  ) => {
    if (selectedEntity.entityId !== undefined) {
      nativeBridge.updatePhysicsComponent(selectedEntity.entityId, kind, patch);
    }
  };

  return (
    <div className="w-full h-full bg-[#0c1017] flex flex-col select-none overflow-hidden">
      {/* Tab Header */}
      <div className="h-8 px-2.5 bg-[#090c12] border-b border-[#19202c] flex items-center justify-between text-[11px] shrink-0">
        <div className="flex items-center gap-1">
          <button
            onClick={() => setActiveTab('Inspector')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors font-medium ${
              activeTab === 'Inspector'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <CubeIcon size={12} />
            <span>Inspector</span>
          </button>
          <button
            onClick={() => setActiveTab('Outliner')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors ${
              activeTab === 'Outliner'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <span>Outliner</span>
          </button>
        </div>

        <button className="p-1 text-slate-400 hover:text-white rounded hover:bg-slate-800">
          <DockIcon size={11} />
        </button>
      </div>

      {/* Main Inspector Scrollable Content */}
      <div className="flex-1 min-h-0 overflow-y-auto p-2.5 space-y-2 scrollbar-thin text-xs text-slate-300">
        {/* Entity Top Header Card */}
        <div className="bg-[#101622] border border-[#1e2738] rounded-md p-2 space-y-2">
          <div className="flex items-center gap-2">
            <div className="w-6 h-6 rounded bg-indigo-950/80 border border-indigo-700/60 flex items-center justify-center text-indigo-400 shrink-0">
              <CubeIcon size={13} />
            </div>
            <input
              type="text"
              value={entityName}
              onChange={(e) => setEntityName(e.target.value)}
              onBlur={handleNameCommit}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  handleNameCommit();
                  (e.target as HTMLInputElement).blur();
                }
              }}
              title="Click to rename entity (Enter to confirm)"
              className="flex-1 bg-[#151c28] border border-[#232f42] hover:border-indigo-500/60 focus:border-indigo-500 rounded px-2 py-0.5 text-slate-100 font-medium text-xs focus:outline-none transition-colors"
            />
            <input
              type="checkbox"
              checked={isEntityActive}
              onChange={(e) => setIsEntityActive(e.target.checked)}
              className="w-3.5 h-3.5 accent-indigo-600 cursor-pointer"
            />
          </div>

          {/* Tag & Layer Dropdowns */}
          <div className="grid grid-cols-2 gap-2 text-[10px]">
            <div className="flex items-center gap-1.5">
              <span className="text-slate-500">Tag:</span>
              <span className="flex-1 bg-[#151c28] border border-[#232f42] rounded px-1.5 py-0.5 text-slate-200 font-mono truncate">
                {tag}
              </span>
            </div>
            <div className="flex items-center gap-1.5">
              <span className="text-slate-500">Layer:</span>
              <span className="flex-1 bg-[#151c28] border border-[#232f42] rounded px-1.5 py-0.5 text-slate-200 font-mono truncate">
                {layer}
              </span>
            </div>
          </div>
        </div>

        {/* COMPONENT 1: TRANSFORM (Always Present) */}
        <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
          <div
            onClick={() => toggleComponent('transform')}
            className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
          >
            <div className="flex items-center gap-1.5">
              <span className="text-indigo-400">❖</span>
              <span>Transform</span>
            </div>
            {collapsed.transform ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
          </div>

          {!collapsed.transform && (
            <div className="p-2 space-y-1.5 text-[11px]">
              {/* Position */}
              <div className="flex items-center gap-1">
                <span className="w-14 text-slate-400 text-[10px]">Position</span>
                <div className="flex-1 grid grid-cols-3 gap-1">
                  {(['x', 'y', 'z'] as const).map((axis, i) => (
                    <div key={axis} className="flex items-center bg-[#151c28] border border-[#232f42] rounded px-1">
                      <span className={`text-[10px] font-bold mr-1 ${i === 0 ? 'text-red-400' : i === 1 ? 'text-green-400' : 'text-blue-400'}`}>
                        {axis.toUpperCase()}
                      </span>
                      <input
                        type="number"
                        step="0.1"
                        value={position[i]}
                        onChange={(e) => onPositionChange(i as 0 | 1 | 2, parseFloat(e.target.value) || 0)}
                        className="w-full bg-transparent text-slate-200 text-[10px] font-mono focus:outline-none text-right"
                      />
                    </div>
                  ))}
                </div>
              </div>

              {/* Rotation */}
              <div className="flex items-center gap-1">
                <span className="w-14 text-slate-400 text-[10px]">Rotation</span>
                <div className="flex-1 grid grid-cols-3 gap-1">
                  {(['x', 'y', 'z'] as const).map((axis, i) => (
                    <div key={axis} className="flex items-center bg-[#151c28] border border-[#232f42] rounded px-1">
                      <span className={`text-[10px] font-bold mr-1 ${i === 0 ? 'text-red-400' : i === 1 ? 'text-green-400' : 'text-blue-400'}`}>
                        {axis.toUpperCase()}
                      </span>
                      <input
                        type="number"
                        step="1"
                        value={rotation[i]}
                        onChange={(e) => onRotationChange(i as 0 | 1 | 2, parseFloat(e.target.value) || 0)}
                        className="w-full bg-transparent text-slate-200 text-[10px] font-mono focus:outline-none text-right"
                      />
                    </div>
                  ))}
                </div>
              </div>

              {/* Scale */}
              <div className="flex items-center gap-1">
                <span className="w-14 text-slate-400 text-[10px]">Scale</span>
                <div className="flex-1 grid grid-cols-3 gap-1">
                  {(['x', 'y', 'z'] as const).map((axis, i) => (
                    <div key={axis} className="flex items-center bg-[#151c28] border border-[#232f42] rounded px-1">
                      <span className={`text-[10px] font-bold mr-1 ${i === 0 ? 'text-red-400' : i === 1 ? 'text-green-400' : 'text-blue-400'}`}>
                        {axis.toUpperCase()}
                      </span>
                      <input
                        type="number"
                        step="0.1"
                        value={scale[i]}
                        onChange={(e) => onScaleChange(i as 0 | 1 | 2, parseFloat(e.target.value) || 0.1)}
                        className="w-full bg-transparent text-slate-200 text-[10px] font-mono focus:outline-none text-right"
                      />
                    </div>
                  ))}
                </div>
              </div>
            </div>
          )}
        </div>

        {/* COMPONENT 2: MESH RENDERER (Live from C++ MeshRendererComponent) */}
        {meshComp && (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('meshRenderer')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-indigo-400">
                <span>🧊</span>
                <span className="text-slate-200">Mesh Renderer</span>
              </div>
              {collapsed.meshRenderer ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.meshRenderer && (
              <div className="p-2 space-y-1 text-[10.5px] text-slate-300">
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Mesh Source</span>
                  <span className="font-mono text-indigo-300 font-semibold">{meshComp.name}</span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Mesh Asset ID</span>
                  <span className="font-mono text-slate-200">0x{meshComp.meshAssetId.toString(16)}</span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Submesh Index</span>
                  <span className="font-mono text-slate-200">{meshComp.submeshIndex}</span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Shadow Casting</span>
                  <span className="text-emerald-400 font-semibold">Enabled</span>
                </div>
              </div>
            )}
          </div>
        )}

        {/* COMPONENT: ANIMATOR (Skeletal Animation) */}
        {animComp && (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('animator')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-purple-400">
                <span>🏃</span>
                <span className="text-slate-200">Animator (Skeletal Animation)</span>
              </div>
              {collapsed.animator ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.animator && (
              <div className="p-2 space-y-1.5 text-[10.5px] text-slate-300">
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Active Clip</span>
                  <span className="font-mono text-purple-300 font-semibold px-1.5 py-0.5 bg-purple-950/50 border border-purple-800/40 rounded text-[10px]">
                    {animComp.clipName || 'Idle.fbx'}
                  </span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Playback State</span>
                  <div className="flex items-center gap-1.5">
                    <span className={`w-2 h-2 rounded-full ${animComp.isPlaying ? 'bg-emerald-400 animate-pulse' : 'bg-amber-400'}`} />
                    <span className={`font-mono font-medium ${animComp.isPlaying ? 'text-emerald-400' : 'text-amber-400'}`}>
                      {animComp.isPlaying ? 'Playing' : 'Paused'}
                    </span>
                  </div>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Speed Multiplier</span>
                  <span className="font-mono text-slate-200">
                    {(animComp.playbackSpeed ?? 1.0).toFixed(2)}x
                  </span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">GPU Bone Palette</span>
                  <span className="font-mono text-indigo-300 text-[10px]">
                    100 Bones (6400 B / 4 KB Ring)
                  </span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Skinning Target</span>
                  <span className="font-mono text-emerald-400 text-[10px]">
                    Root Joint: mixamorig:Hips
                  </span>
                </div>
              </div>
            )}
          </div>
        )}

        {/* COMPONENT 3: RIGIDBODY & COLLIDER (Jolt Physics) */}
        {rbComp ? (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('rigidbody')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-sky-400">
                <span>🌐</span>
                <span className="text-slate-200">Rigidbody &amp; Collider (Jolt)</span>
              </div>
              <div className="flex items-center gap-1.5">
                {rbComp.motionType === 'Dynamic' && (
                  <span
                    className={`px-1.5 py-0.2 rounded text-[9px] font-mono ${
                      rbComp.isAwake
                        ? 'bg-emerald-950/80 text-emerald-400 border border-emerald-700/50'
                        : 'bg-slate-800 text-slate-400 border border-slate-700/50'
                    }`}
                  >
                    {rbComp.isAwake ? 'Awake' : 'Sleeping'}
                  </span>
                )}
                {collapsed.rigidbody ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
              </div>
            </div>
            {!collapsed.rigidbody && (
              <div className="p-2 space-y-1.5 text-[10.5px] text-slate-300">
                <div className="flex justify-between items-center gap-2">
                  <span className="text-slate-400">Motion Type</span>
                  <select
                    value={rbComp.motionType || 'Dynamic'}
                    onChange={(e) => commitPhysicsPatch('rigidbody', { motionType: e.target.value })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 text-[10.5px] text-slate-200 focus:outline-none focus:border-sky-500"
                  >
                    <option value="Static">Static</option>
                    <option value="Kinematic">Kinematic</option>
                    <option value="Dynamic">Dynamic</option>
                  </select>
                </div>

                <div className="flex justify-between items-center gap-2">
                  <span className="text-slate-400">Collider Shape</span>
                  <select
                    value={rbComp.shape || 'Box'}
                    onChange={(e) => commitPhysicsPatch('rigidbody', { shape: e.target.value })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 text-[10.5px] text-slate-200 focus:outline-none focus:border-sky-500"
                  >
                    <option value="Box">Box</option>
                    <option value="Sphere">Sphere</option>
                    <option value="Capsule">Capsule</option>
                  </select>
                </div>

                <div className="grid grid-cols-2 gap-1.5">
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Mass (kg)</span>
                    <input
                      type="number"
                      step="0.5"
                      defaultValue={rbComp.mass.toFixed(2)}
                      key={`mass-${selectedEntity.entityId}-${rbComp.mass}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { mass: Math.max(0.01, Number(e.target.value) || 1) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Density (kg/m3)</span>
                    <input
                      type="number"
                      step="50"
                      defaultValue={(rbComp.density ?? 1000).toFixed(1)}
                      key={`dens-${selectedEntity.entityId}-${rbComp.density}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { density: Math.max(0.01, Number(e.target.value) || 1000) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex items-center gap-1.5 col-span-2 cursor-pointer pt-0.5">
                    <input
                      type="checkbox"
                      checked={Boolean(rbComp.useDensity)}
                      onChange={(e) => commitPhysicsPatch('rigidbody', { useDensity: e.target.checked })}
                      className="rounded border-slate-600 bg-[#090d14] text-sky-500"
                    />
                    <span className="text-[10px] text-slate-300">Use Density (Compute Mass from Shape Volume)</span>
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Friction</span>
                    <input
                      type="number"
                      step="0.05"
                      defaultValue={rbComp.friction.toFixed(2)}
                      key={`fric-${selectedEntity.entityId}-${rbComp.friction}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { friction: Math.max(0, Number(e.target.value) || 0) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Restitution</span>
                    <input
                      type="number"
                      step="0.05"
                      min="0"
                      max="1"
                      defaultValue={rbComp.restitution.toFixed(2)}
                      key={`rest-${selectedEntity.entityId}-${rbComp.restitution}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { restitution: Math.min(1, Math.max(0, Number(e.target.value) || 0)) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Linear Damping</span>
                    <input
                      type="number"
                      step="0.05"
                      defaultValue={rbComp.linearDamping.toFixed(2)}
                      key={`ldamp-${selectedEntity.entityId}-${rbComp.linearDamping}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { linearDamping: Math.max(0, Number(e.target.value) || 0) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Angular Damping</span>
                    <input
                      type="number"
                      step="0.05"
                      defaultValue={rbComp.angularDamping.toFixed(2)}
                      key={`adamp-${selectedEntity.entityId}-${rbComp.angularDamping}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { angularDamping: Math.max(0, Number(e.target.value) || 0) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Gravity Factor</span>
                    <input
                      type="number"
                      step="0.1"
                      defaultValue={rbComp.gravityFactor.toFixed(2)}
                      key={`gfac-${selectedEntity.entityId}-${rbComp.gravityFactor}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { gravityFactor: Number(e.target.value) ?? 1 })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                </div>

                <div>
                  <span className="text-[9.5px] text-slate-400 block mb-0.5">
                    {rbComp.shape === 'Sphere'
                      ? 'Radius (X)'
                      : rbComp.shape === 'Capsule'
                      ? 'Radius (X) / HalfHeight (Y)'
                      : 'Half Extents (X / Y / Z)'}
                  </span>
                  <div className="grid grid-cols-3 gap-1">
                    {([0, 1, 2] as const).map((idx) => (
                      <input
                        key={`he-${idx}-${selectedEntity.entityId}-${rbComp.halfExtents[idx]}`}
                        type="number"
                        step="0.05"
                        defaultValue={rbComp.halfExtents[idx].toFixed(2)}
                        onBlur={(e) => {
                          const next: [number, number, number] = [...rbComp.halfExtents];
                          next[idx] = Math.max(0.01, Number(e.target.value) || 0.5);
                          commitPhysicsPatch('rigidbody', { halfExtents: next });
                        }}
                        className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[10px] text-slate-200"
                      />
                    ))}
                  </div>
                </div>

                <div>
                  <span className="text-[9.5px] text-slate-400 block mb-0.5">Collider Offset (X / Y / Z)</span>
                  <div className="grid grid-cols-3 gap-1">
                    {([0, 1, 2] as const).map((idx) => (
                      <input
                        key={`coff-${idx}-${selectedEntity.entityId}-${rbComp.colliderOffset[idx]}`}
                        type="number"
                        step="0.05"
                        defaultValue={rbComp.colliderOffset[idx].toFixed(2)}
                        onBlur={(e) => {
                          const next: [number, number, number] = [...rbComp.colliderOffset];
                          next[idx] = Number(e.target.value) || 0;
                          commitPhysicsPatch('rigidbody', { colliderOffset: next });
                        }}
                        className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[10px] text-slate-200"
                      />
                    ))}
                  </div>
                </div>

                <div>
                  <span className="text-[9.5px] text-slate-400 block mb-0.5">Collider Rotation Euler (Deg)</span>
                  <div className="grid grid-cols-3 gap-1">
                    {([0, 1, 2] as const).map((idx) => {
                      const rotEuler: [number, number, number] = rbComp.colliderRotationEuler || [0, 0, 0];
                      return (
                        <input
                          key={`crot-${idx}-${selectedEntity.entityId}-${rotEuler[idx]}`}
                          type="number"
                          step="1"
                          defaultValue={rotEuler[idx].toFixed(1)}
                          onBlur={(e) => {
                            const next: [number, number, number] = [...rotEuler];
                            next[idx] = Number(e.target.value) || 0;
                            commitPhysicsPatch('rigidbody', { colliderRotationEuler: next });
                          }}
                          className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[10px] text-slate-200"
                        />
                      );
                    })}
                  </div>
                </div>

                <div className="grid grid-cols-2 gap-1.5">
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Collision Layer</span>
                    <input
                      type="number"
                      min="0"
                      defaultValue={rbComp.collisionLayer}
                      key={`clay-${selectedEntity.entityId}-${rbComp.collisionLayer}`}
                      onBlur={(e) => commitPhysicsPatch('rigidbody', { collisionLayer: Math.max(0, Number(e.target.value) >>> 0) })}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                  <label className="flex flex-col gap-0.5">
                    <span className="text-[9.5px] text-slate-400">Collision Mask (Hex)</span>
                    <input
                      type="text"
                      defaultValue={`0x${(rbComp.collisionMask >>> 0).toString(16).toUpperCase()}`}
                      key={`cmsk-${selectedEntity.entityId}-${rbComp.collisionMask}`}
                      onBlur={(e) => {
                        const raw = e.target.value.trim();
                        const parsed = raw.startsWith('0x') || raw.startsWith('0X')
                          ? parseInt(raw, 16)
                          : Number(raw);
                        if (!Number.isNaN(parsed)) {
                          commitPhysicsPatch('rigidbody', { collisionMask: parsed >>> 0 });
                        }
                      }}
                      className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                    />
                  </label>
                </div>

                <div>
                  <span className="text-[9.5px] text-slate-400 block mb-1">Locked Axes (Position / Rotation)</span>
                  <div className="grid grid-cols-6 gap-1">
                    {(['PX', 'PY', 'PZ', 'RX', 'RY', 'RZ'] as const).map((label, bit) => {
                      const mask = 1 << bit;
                      const currentBits = rbComp.lockedAxes || 0;
                      const active = (currentBits & mask) !== 0;
                      return (
                        <button
                          key={label}
                          type="button"
                          onClick={() => commitPhysicsPatch('rigidbody', { lockedAxes: currentBits ^ mask })}
                          className={`py-0.5 rounded text-[9.5px] font-mono border transition-colors ${
                            active
                              ? 'bg-sky-600/30 border-sky-500 text-sky-300 font-semibold'
                              : 'bg-[#090d14] border-[#1e2738] text-slate-400 hover:text-slate-200'
                          }`}
                        >
                          {label}
                        </button>
                      );
                    })}
                  </div>
                </div>

                <div className="flex items-center justify-between pt-1">
                  <label className="flex items-center gap-1.5 cursor-pointer">
                    <input
                      type="checkbox"
                      checked={rbComp.useGravity}
                      onChange={(e) => commitPhysicsPatch('rigidbody', { useGravity: e.target.checked })}
                      className="rounded border-slate-600 bg-[#090d14] text-sky-500"
                    />
                    <span className="text-[10px] text-slate-300">Gravity</span>
                  </label>
                  <label className="flex items-center gap-1.5 cursor-pointer">
                    <input
                      type="checkbox"
                      checked={rbComp.useCCD}
                      onChange={(e) => commitPhysicsPatch('rigidbody', { useCCD: e.target.checked })}
                      className="rounded border-slate-600 bg-[#090d14] text-sky-500"
                    />
                    <span className="text-[10px] text-slate-300">CCD</span>
                  </label>
                  <label className="flex items-center gap-1.5 cursor-pointer">
                    <input
                      type="checkbox"
                      checked={rbComp.isTrigger}
                      onChange={(e) => commitPhysicsPatch('rigidbody', { isTrigger: e.target.checked })}
                      className="rounded border-slate-600 bg-[#090d14] text-amber-500"
                    />
                    <span className="text-[10px] text-slate-300">Trigger</span>
                  </label>
                </div>
              </div>
            )}
          </div>
        ) : null}

        {/* COMPONENT 3A: COMPOUND COLLIDER SUB-SHAPES */}
        {compoundList.length > 0 ? (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('compoundColliders')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-cyan-400">
                <span>🧩</span>
                <span className="text-slate-200">Compound Colliders</span>
                <span className="px-1.5 py-0.2 rounded text-[9px] font-mono bg-cyan-950/70 text-cyan-300 border border-cyan-700/40">
                  {compoundList.length}
                </span>
              </div>
              {collapsed.compoundColliders ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.compoundColliders && (
              <div className="p-2 space-y-2 text-[10px] text-slate-300">
                {compoundList.map((sub, idx) => (
                  <div key={`cc-${selectedEntity.entityId}-${idx}`} className="p-1.5 rounded bg-[#0a0f18] border border-[#1b2435] space-y-1">
                    <div className="flex items-center justify-between gap-1">
                      <div className="flex items-center gap-1.5">
                        <span className="font-mono text-[9.5px] text-cyan-400">#{idx}</span>
                        <select
                          value={sub.shape}
                          onChange={(e) => commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, shape: e.target.value })}
                          className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 text-[10px] text-slate-200"
                        >
                          <option value="Box">Box</option>
                          <option value="Sphere">Sphere</option>
                          <option value="Capsule">Capsule</option>
                        </select>
                      </div>
                      <button
                        type="button"
                        onClick={() => commitPhysicsPatch('compoundColliders', { action: 'remove', index: idx })}
                        className="px-1.5 py-0.5 rounded bg-rose-950/60 hover:bg-rose-800/70 text-rose-300 border border-rose-700/40 text-[9px]"
                      >
                        Remove
                      </button>
                    </div>
                    <div className="grid grid-cols-3 gap-1">
                      {([0, 1, 2] as const).map((ax) => (
                        <input
                          key={`cche-${idx}-${ax}-${sub.halfExtents[ax]}`}
                          type="number"
                          step="0.05"
                          title="Half Extents"
                          defaultValue={sub.halfExtents[ax].toFixed(2)}
                          onBlur={(e) => {
                            const next: [number, number, number] = [...sub.halfExtents];
                            next[ax] = Math.max(0.01, Number(e.target.value) || 0.25);
                            commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, halfExtents: next });
                          }}
                          className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                        />
                      ))}
                    </div>
                    <div className="grid grid-cols-3 gap-1">
                      {([0, 1, 2] as const).map((ax) => (
                        <input
                          key={`ccoff-${idx}-${ax}-${sub.localOffset[ax]}`}
                          type="number"
                          step="0.05"
                          title="Local Offset"
                          defaultValue={sub.localOffset[ax].toFixed(2)}
                          onBlur={(e) => {
                            const next: [number, number, number] = [...sub.localOffset];
                            next[ax] = Number(e.target.value) || 0;
                            commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, localOffset: next });
                          }}
                          className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                        />
                      ))}
                    </div>
                    <div className="grid grid-cols-3 gap-1">
                      {([0, 1, 2] as const).map((ax) => (
                        <input
                          key={`ccrot-${idx}-${ax}-${sub.localRotationEuler[ax]}`}
                          type="number"
                          step="1"
                          title="Local Rotation Euler (Deg)"
                          defaultValue={sub.localRotationEuler[ax].toFixed(1)}
                          onBlur={(e) => {
                            const next: [number, number, number] = [...sub.localRotationEuler];
                            next[ax] = Number(e.target.value) || 0;
                            commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, localRotationEuler: next });
                          }}
                          className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                        />
                      ))}
                    </div>
                    <div className="flex items-center justify-between gap-1.5 pt-0.5">
                      <input
                        type="number"
                        min="0"
                        title="Layer"
                        defaultValue={sub.collisionLayer}
                        key={`cclay-${idx}-${sub.collisionLayer}`}
                        onBlur={(e) => commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, collisionLayer: Math.max(0, Number(e.target.value) >>> 0) })}
                        className="w-16 bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                      />
                      <input
                        type="text"
                        title="Mask"
                        defaultValue={`0x${(sub.collisionMask >>> 0).toString(16).toUpperCase()}`}
                        key={`ccmsk-${idx}-${sub.collisionMask}`}
                        onBlur={(e) => {
                          const raw = e.target.value.trim();
                          const p = raw.startsWith('0x') || raw.startsWith('0X') ? parseInt(raw, 16) : Number(raw);
                          if (!Number.isNaN(p)) {
                            commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, collisionMask: p >>> 0 });
                          }
                        }}
                        className="flex-1 bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                      />
                      <label className="flex items-center gap-1 cursor-pointer">
                        <input
                          type="checkbox"
                          checked={sub.isTrigger}
                          onChange={(e) => commitPhysicsPatch('compoundColliders', { action: 'update', index: idx, ...sub, isTrigger: e.target.checked })}
                          className="rounded border-slate-600 bg-[#090d14] text-amber-500"
                        />
                        <span className="text-[9.5px] text-slate-300">Trig</span>
                      </label>
                    </div>
                  </div>
                ))}
                <button
                  type="button"
                  onClick={() => commitPhysicsPatch('compoundColliders', { action: 'add', shape: 'Box', halfExtents: [0.35, 0.35, 0.35], localOffset: [0, 0.5, 0] })}
                  className="w-full py-1 rounded bg-[#131b28] hover:bg-cyan-600/20 border border-[#222d3e] text-cyan-300 text-[10px] font-medium"
                >
                  + Add Sub-Shape
                </button>
              </div>
            )}
          </div>
        ) : null}

        {/* COMPONENT 3C: PHYSICS JOINTS (Jolt Constraints) */}
        {jointList.length > 0 ? (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('physicsJoints')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-amber-400">
                <span>🔗</span>
                <span className="text-slate-200">Physics Joints (Jolt)</span>
                <span className="px-1.5 py-0.2 rounded text-[9px] font-mono bg-amber-950/70 text-amber-300 border border-amber-700/40">
                  {jointList.length}
                </span>
              </div>
              {collapsed.physicsJoints ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.physicsJoints && (
              <div className="p-2 space-y-2 text-[10px] text-slate-300">
                {jointList.map((jt, idx) => (
                  <div key={`jt-${selectedEntity.entityId}-${idx}`} className="p-1.5 rounded bg-[#0a0f18] border border-[#1b2435] space-y-1.5">
                    <div className="flex items-center justify-between gap-1">
                      <div className="flex items-center gap-1.5">
                        <select
                          value={jt.type}
                          onChange={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, jointType: e.target.value })}
                          className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 text-[10px] text-slate-200 font-semibold"
                        >
                          <option value="Fixed">Fixed</option>
                          <option value="Distance">Distance</option>
                          <option value="Hinge">Hinge</option>
                          <option value="Slider">Slider</option>
                        </select>
                        {jt.broken && (
                          <span className="px-1 py-0.2 rounded text-[8.5px] font-mono bg-rose-950 text-rose-400 border border-rose-700/50">
                            BROKEN
                          </span>
                        )}
                      </div>
                      <button
                        type="button"
                        onClick={() => commitPhysicsPatch('physicsJoints', { action: 'remove', index: idx })}
                        className="px-1.5 py-0.5 rounded bg-rose-950/60 hover:bg-rose-800/70 text-rose-300 border border-rose-700/40 text-[9px]"
                      >
                        Remove
                      </button>
                    </div>

                    <div className="flex items-center justify-between gap-1.5">
                      <span className="text-[9.5px] text-slate-400">Connected Entity (-1=World)</span>
                      <div className="flex items-center gap-1">
                        <span className="text-[9px] text-slate-500 truncate max-w-[80px]">{jt.connectedEntityName}</span>
                        <input
                          type="number"
                          defaultValue={jt.connectedEntityId}
                          key={`jtconn-${idx}-${jt.connectedEntityId}`}
                          onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, connectedEntityId: Number(e.target.value) })}
                          className="w-14 bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                        />
                      </div>
                    </div>

                    <div>
                      <span className="text-[9px] text-slate-400 block mb-0.5">Anchor A (World)</span>
                      <div className="grid grid-cols-3 gap-1">
                        {([0, 1, 2] as const).map((ax) => (
                          <input
                            key={`jtaa-${idx}-${ax}-${jt.anchorA[ax]}`}
                            type="number"
                            step="0.1"
                            defaultValue={jt.anchorA[ax].toFixed(2)}
                            onBlur={(e) => {
                              const next: [number, number, number] = [...jt.anchorA];
                              next[ax] = Number(e.target.value) || 0;
                              commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, anchorA: next });
                            }}
                            className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                          />
                        ))}
                      </div>
                    </div>

                    {(jt.type === 'Hinge' || jt.type === 'Slider') && (
                      <div>
                        <span className="text-[9px] text-slate-400 block mb-0.5">Axis (X / Y / Z)</span>
                        <div className="grid grid-cols-3 gap-1">
                          {([0, 1, 2] as const).map((ax) => (
                            <input
                              key={`jtax-${idx}-${ax}-${jt.axis[ax]}`}
                              type="number"
                              step="0.1"
                              defaultValue={jt.axis[ax].toFixed(2)}
                              onBlur={(e) => {
                                const next: [number, number, number] = [...jt.axis];
                                next[ax] = Number(e.target.value) || 0;
                                commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, axis: next });
                              }}
                              className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          ))}
                        </div>
                      </div>
                    )}

                    <div className="grid grid-cols-2 gap-1.5">
                      {jt.type === 'Distance' ? (
                        <>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Min Dist</span>
                            <input
                              type="number"
                              step="0.1"
                              defaultValue={jt.minDistance.toFixed(2)}
                              key={`jtmind-${idx}-${jt.minDistance}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, minDistance: Number(e.target.value) })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Max Dist</span>
                            <input
                              type="number"
                              step="0.1"
                              defaultValue={jt.maxDistance.toFixed(2)}
                              key={`jtmaxd-${idx}-${jt.maxDistance}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, maxDistance: Number(e.target.value) })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                        </>
                      ) : jt.type !== 'Fixed' ? (
                        <>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Min Limit</span>
                            <input
                              type="number"
                              step="0.1"
                              defaultValue={jt.minLimit.toFixed(2)}
                              key={`jtminl-${idx}-${jt.minLimit}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, minLimit: Number(e.target.value) || 0 })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Max Limit</span>
                            <input
                              type="number"
                              step="0.1"
                              defaultValue={jt.maxLimit.toFixed(2)}
                              key={`jtmaxl-${idx}-${jt.maxLimit}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, maxLimit: Number(e.target.value) || 0 })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                        </>
                      ) : null}

                      <label className="flex flex-col gap-0.5">
                        <span className="text-[9px] text-slate-400">Break Force (N)</span>
                        <input
                          type="number"
                          step="10"
                          defaultValue={jt.breakForce.toFixed(1)}
                          key={`jtbf-${idx}-${jt.breakForce}`}
                          onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, breakForce: Math.max(0, Number(e.target.value) || 0) })}
                          className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                        />
                      </label>
                      <label className="flex flex-col gap-0.5">
                        <span className="text-[9px] text-slate-400">Break Torque</span>
                        <input
                          type="number"
                          step="10"
                          defaultValue={jt.breakTorque.toFixed(1)}
                          key={`jtbt-${idx}-${jt.breakTorque}`}
                          onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, breakTorque: Math.max(0, Number(e.target.value) || 0) })}
                          className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                        />
                      </label>
                      {jt.type !== 'Fixed' && (
                        <>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Max Friction</span>
                            <input
                              type="number"
                              step="0.5"
                              defaultValue={jt.maxFriction.toFixed(2)}
                              key={`jtfr-${idx}-${jt.maxFriction}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, maxFriction: Math.max(0, Number(e.target.value) || 0) })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Spring Hz</span>
                            <input
                              type="number"
                              step="0.5"
                              defaultValue={jt.springFrequencyHz.toFixed(2)}
                              key={`jtfq-${idx}-${jt.springFrequencyHz}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, springFrequencyHz: Math.max(0, Number(e.target.value) || 0) })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                          <label className="flex flex-col gap-0.5">
                            <span className="text-[9px] text-slate-400">Spring Damping</span>
                            <input
                              type="number"
                              step="0.05"
                              defaultValue={jt.springDampingRatio.toFixed(2)}
                              key={`jtdp-${idx}-${jt.springDampingRatio}`}
                              onBlur={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, springDampingRatio: Math.max(0, Number(e.target.value) || 0) })}
                              className="w-full bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          </label>
                        </>
                      )}
                    </div>

                    {!jt.autoDetectAnchors && (
                      <div>
                        <span className="text-[9px] text-slate-400 block mb-0.5">Anchor B (World)</span>
                        <div className="grid grid-cols-3 gap-1">
                          {([0, 1, 2] as const).map((ax) => (
                            <input
                              key={`jtab-${idx}-${ax}-${jt.anchorB[ax]}`}
                              type="number"
                              step="0.1"
                              defaultValue={jt.anchorB[ax].toFixed(2)}
                              onBlur={(e) => {
                                const next: [number, number, number] = [...jt.anchorB];
                                next[ax] = Number(e.target.value) || 0;
                                commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, anchorB: next });
                              }}
                              className="bg-[#090d14] border border-[#1e2738] rounded px-1 py-0.5 font-mono text-[9.5px] text-slate-200"
                            />
                          ))}
                        </div>
                      </div>
                    )}

                    <div className="flex items-center justify-between pt-0.5">
                      <label className="flex items-center gap-1 cursor-pointer">
                        <input
                          type="checkbox"
                          checked={jt.autoDetectAnchors}
                          onChange={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, autoDetectAnchors: e.target.checked })}
                          className="rounded border-slate-600 bg-[#090d14] text-amber-500"
                        />
                        <span className="text-[9.5px] text-slate-300">Auto Anchors</span>
                      </label>
                      <label className="flex items-center gap-1 cursor-pointer">
                        <input
                          type="checkbox"
                          checked={jt.enabled}
                          onChange={(e) => commitPhysicsPatch('physicsJoints', { action: 'update', index: idx, ...jt, enabled: e.target.checked })}
                          className="rounded border-slate-600 bg-[#090d14] text-emerald-500"
                        />
                        <span className="text-[9.5px] text-slate-300">Enabled</span>
                      </label>
                    </div>
                  </div>
                ))}
                <button
                  type="button"
                  onClick={() => commitPhysicsPatch('physicsJoints', { action: 'add', jointType: 'Hinge' })}
                  className="w-full py-1 rounded bg-[#131b28] hover:bg-amber-600/20 border border-[#222d3e] text-amber-300 text-[10px] font-medium"
                >
                  + Add Joint
                </button>
              </div>
            )}
          </div>
        ) : null}

        {/* COMPONENT 3B: CHARACTER CONTROLLER (Jolt CharacterVirtual) */}
        {charComp ? (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('characterController')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-emerald-400">
                <span>🏃</span>
                <span className="text-slate-200">Character Controller (Jolt)</span>
              </div>
              <div className="flex items-center gap-1.5">
                <span
                  className={`px-1.5 py-0.2 rounded text-[9px] font-mono ${
                    charComp.isGrounded
                      ? 'bg-emerald-950/80 text-emerald-400 border border-emerald-700/50'
                      : 'bg-slate-800 text-slate-400 border border-slate-700/50'
                  }`}
                >
                  {charComp.isGrounded ? 'Grounded' : charComp.isSliding ? 'Sliding' : 'In Air'}
                </span>
                {collapsed.characterController ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
              </div>
            </div>
            {!collapsed.characterController && (
              <div className="p-2 grid grid-cols-2 gap-1.5 text-[10.5px] text-slate-300">
                <label className="flex flex-col gap-0.5">
                  <span className="text-[9.5px] text-slate-400">Radius (m)</span>
                  <input
                    type="number"
                    step="0.05"
                    defaultValue={charComp.radius.toFixed(2)}
                    key={`cc-rad-${selectedEntity.entityId}-${charComp.radius}`}
                    onBlur={(e) => commitPhysicsPatch('characterController', { radius: Math.max(0.05, Number(e.target.value) || 0.35) })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                  />
                </label>
                <label className="flex flex-col gap-0.5">
                  <span className="text-[9.5px] text-slate-400">Height (m)</span>
                  <input
                    type="number"
                    step="0.05"
                    defaultValue={charComp.height.toFixed(2)}
                    key={`cc-h-${selectedEntity.entityId}-${charComp.height}`}
                    onBlur={(e) => commitPhysicsPatch('characterController', { height: Math.max(0.2, Number(e.target.value) || 1.8) })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                  />
                </label>
                <label className="flex flex-col gap-0.5">
                  <span className="text-[9.5px] text-slate-400">Step Height (m)</span>
                  <input
                    type="number"
                    step="0.05"
                    defaultValue={charComp.stepHeight.toFixed(2)}
                    key={`cc-sh-${selectedEntity.entityId}-${charComp.stepHeight}`}
                    onBlur={(e) => commitPhysicsPatch('characterController', { stepHeight: Math.max(0, Number(e.target.value) || 0) })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                  />
                </label>
                <label className="flex flex-col gap-0.5">
                  <span className="text-[9.5px] text-slate-400">Max Slope (°)</span>
                  <input
                    type="number"
                    step="1"
                    defaultValue={charComp.maxSlopeAngleDegrees.toFixed(1)}
                    key={`cc-sl-${selectedEntity.entityId}-${charComp.maxSlopeAngleDegrees}`}
                    onBlur={(e) => commitPhysicsPatch('characterController', { maxSlopeAngleDegrees: Math.min(89, Math.max(1, Number(e.target.value) || 45)) })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                  />
                </label>
                <label className="flex flex-col gap-0.5">
                  <span className="text-[9.5px] text-slate-400">Push Strength (N)</span>
                  <input
                    type="number"
                    step="10"
                    defaultValue={charComp.maxPushStrength.toFixed(0)}
                    key={`cc-ps-${selectedEntity.entityId}-${charComp.maxPushStrength}`}
                    onBlur={(e) => commitPhysicsPatch('characterController', { maxPushStrength: Math.max(0, Number(e.target.value) || 0) })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                  />
                </label>
                <label className="flex flex-col gap-0.5">
                  <span className="text-[9.5px] text-slate-400">Air Control</span>
                  <input
                    type="number"
                    step="0.05"
                    defaultValue={charComp.airControl.toFixed(2)}
                    key={`cc-ac-${selectedEntity.entityId}-${charComp.airControl}`}
                    onBlur={(e) => commitPhysicsPatch('characterController', { airControl: Math.min(1, Math.max(0, Number(e.target.value) || 0)) })}
                    className="bg-[#090d14] border border-[#1e2738] rounded px-1.5 py-0.5 font-mono text-[10.5px] text-slate-200"
                  />
                </label>
              </div>
            )}
          </div>
        ) : null}

        {/* COMPONENT 4: C# SCRIPT */}
        {scriptComp && (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('script')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-amber-400">
                <span>⚡</span>
                <span className="text-slate-200">C# Script (.NET Core)</span>
              </div>
              {collapsed.script ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.script && (
              <div className="p-2 space-y-1 text-[10.5px] text-slate-300">
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Script Class</span>
                  <span className="font-mono text-amber-300 font-semibold">{scriptComp.className}</span>
                </div>
                {scriptComp.namespace && (
                  <div className="flex justify-between items-center py-0.5">
                    <span className="text-slate-400">Namespace</span>
                    <span className="font-mono text-slate-300">{scriptComp.namespace}</span>
                  </div>
                )}
              </div>
            )}
          </div>
        )}

        {/* COMPONENT 5: LIGHT */}
        {lightComp && (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('light')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-amber-300">
                <span>💡</span>
                <span className="text-slate-200">Light Component</span>
              </div>
              {collapsed.light ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.light && (
              <div className="p-2 space-y-1 text-[10.5px] text-slate-300">
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Type</span>
                  <span className="font-mono text-amber-200">{lightComp.type}</span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Intensity</span>
                  <span className="font-mono text-slate-200">{lightComp.intensity} lx</span>
                </div>
              </div>
            )}
          </div>
        )}

        {/* COMPONENT 6: CAMERA */}
        {cameraComp && (
          <div className="bg-[#101622] border border-[#1e2738] rounded-md overflow-hidden">
            <div
              onClick={() => toggleComponent('camera')}
              className="px-2.5 py-1.5 bg-[#131b28] border-b border-[#1e2738] flex items-center justify-between cursor-pointer text-[11px] font-semibold text-slate-200"
            >
              <div className="flex items-center gap-1.5 text-sky-400">
                <span>📷</span>
                <span className="text-slate-200">Camera Component</span>
              </div>
              {collapsed.camera ? <ChevronRightIcon size={10} /> : <ChevronDownIcon size={10} />}
            </div>
            {!collapsed.camera && (
              <div className="p-2 space-y-1 text-[10.5px] text-slate-300">
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Field of View (FOV)</span>
                  <span className="font-mono text-sky-300 font-semibold">{cameraComp.fov ?? 60}°</span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Clipping Planes</span>
                  <span className="font-mono text-slate-300">{cameraComp.near ?? 0.1}m - {cameraComp.far ?? 1000}m</span>
                </div>
                <div className="flex justify-between items-center py-0.5">
                  <span className="text-slate-400">Status</span>
                  <span className="font-mono text-emerald-400 font-semibold">Active Game Viewport Camera</span>
                </div>
              </div>
            )}
          </div>
        )}

        {/* + Add Component Button & Dropdown Picker */}
        <div className="relative" ref={addCompRef}>
          <button
            onClick={() => setAddComponentOpen(!addComponentOpen)}
            aria-haspopup="menu"
            aria-expanded={addComponentOpen}
            className="w-full py-1.5 bg-[#131b28] hover:bg-indigo-600/30 text-slate-300 hover:text-white border border-[#222d3e] hover:border-indigo-500/60 rounded text-[11px] font-medium transition-all flex items-center justify-center gap-1.5"
          >
            <PlusIcon size={12} />
            <span>Add Component</span>
          </button>

          {addComponentOpen && (
            <div ref={addCompDropdownRef} className="absolute bottom-full left-0 mb-1 w-full bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md text-xs">
              <div className="px-3 py-1 text-[10px] text-slate-400 font-mono border-b border-[#1f293d]">
                SELECT COMPONENT
              </div>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'meshRenderer');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>🧊</span>
                <span>Mesh Renderer</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'animator');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>🏃</span>
                <span>Animator (Skeletal Animation)</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'rigidbody');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>⚡</span>
                <span>Rigidbody (Jolt Physics)</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'characterController');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>🏃</span>
                <span>Character Controller (Jolt Virtual)</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'compoundCollider');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>🧩</span>
                <span>Compound Collider (Sub-Shapes)</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'physicsJoint');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>🔗</span>
                <span>Physics Joint (Jolt)</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'light');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>💡</span>
                <span>Light Component</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'camera');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>📷</span>
                <span>Camera Component</span>
              </button>
              <button
                onClick={() => {
                  setAddComponentOpen(false);
                  if (selectedEntity.entityId !== undefined) {
                    onAddComponent?.(selectedEntity.entityId, 'script');
                  }
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left"
              >
                <span>📜</span>
                <span>C# Script (.NET 8)</span>
              </button>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
