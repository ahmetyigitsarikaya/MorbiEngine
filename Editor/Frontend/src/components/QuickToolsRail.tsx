import React from 'react';
import {
  SelectGizmoIcon,
  TranslateGizmoIcon,
  RotateGizmoIcon,
  ScaleGizmoIcon,
  BrushGizmoIcon,
  TreeGizmoIcon,
  MagnetGizmoIcon,
  SettingsIcon
} from './Icons';
import { GizmoTool } from '../types';

interface QuickToolsRailProps {
  activeTool: GizmoTool;
  onSelectTool: (tool: GizmoTool) => void;
}

export const QuickToolsRail: React.FC<QuickToolsRailProps> = ({
  activeTool,
  onSelectTool
}) => {
  const tools: { id: GizmoTool; label: string; icon: React.ReactNode; shortcut?: string }[] = [
    { id: 'select', label: 'Selection Tool (Q)', icon: <SelectGizmoIcon size={14} />, shortcut: 'Q' },
    { id: 'translate', label: 'Move / Translate Gizmo (W)', icon: <TranslateGizmoIcon size={14} />, shortcut: 'W' },
    { id: 'rotate', label: 'Rotate Gizmo (E)', icon: <RotateGizmoIcon size={14} />, shortcut: 'E' },
    { id: 'scale', label: 'Scale Gizmo (R)', icon: <ScaleGizmoIcon size={14} />, shortcut: 'R' },
    { id: 'sculpt', label: 'Terrain Sculpt Brush', icon: <BrushGizmoIcon size={14} /> },
    { id: 'foliage', label: 'Foliage & Tree Density Tool', icon: <TreeGizmoIcon size={14} /> },
    { id: 'magnet', label: 'Physics Snap / Surface Magnet', icon: <MagnetGizmoIcon size={14} /> }
  ];

  return (
    <aside className="w-9 bg-[#090c12] border-r border-[#19202c] flex flex-col items-center py-2 gap-1.5 select-none z-20">
      {tools.map((tool) => {
        const isActive = activeTool === tool.id;
        return (
          <button
            key={tool.id}
            onClick={() => onSelectTool(tool.id)}
            title={tool.label}
            className={`w-7 h-7 flex items-center justify-center rounded transition-all ${
              isActive
                ? 'bg-indigo-600 text-white shadow-sm shadow-indigo-900/60'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            {tool.icon}
          </button>
        );
      })}

      <div className="w-5 h-[1px] bg-[#1e2738] my-1" />

      <button
        title="Viewport Gizmo Settings"
        onClick={() => onSelectTool('settings')}
        className={`w-7 h-7 flex items-center justify-center rounded transition-all ${
          activeTool === 'settings'
            ? 'bg-indigo-600 text-white'
            : 'text-slate-500 hover:text-slate-300 hover:bg-[#141b27]'
        }`}
      >
        <SettingsIcon size={13} />
      </button>
    </aside>
  );
};
