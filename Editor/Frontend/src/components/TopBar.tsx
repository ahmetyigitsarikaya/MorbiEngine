import React from 'react';
import { nativeBridge } from '../bridge/nativeBridge';
import {
  MorbiLogo,
  PlayIcon,
  PauseIcon,
  StopIcon,
  StepIcon,
  DiscordIcon,
  GithubIcon,
  SettingsIcon,
  FolderIcon,
  ChevronDownIcon
} from './Icons';

export type EditorPlayMode = 'edit' | 'playing' | 'paused';

interface TopBarProps {
  playMode: EditorPlayMode;
  onPlay: () => void;
  onPause: () => void;
  onStop: () => void;
  onStep?: () => void;
  onNewScene?: () => void;
  onOpenSceneDialog?: () => void;
  onSaveScene?: () => void;
  onSaveSceneAs?: () => void;
  onCreateEntity?: (name: string, type: string) => void;
  onAddComponent?: (type: string) => void;
  onImportModel?: () => void;
  onUndo?: () => void;
  onRedo?: () => void;
  onCopy?: () => void;
  onPaste?: () => void;
  onDuplicate?: () => void;
  onCommand?: (cmd: string) => void;
  onCompileScripts?: () => void;
  onBuildGame?: () => void;
  projectName?: string;
  sceneName?: string;
}

export const TopBar: React.FC<TopBarProps> = ({
  playMode,
  onPlay,
  onPause,
  onStop,
  onStep,
  onNewScene,
  onOpenSceneDialog,
  onSaveScene,
  onSaveSceneAs,
  onCreateEntity,
  onAddComponent,
  onImportModel,
  onUndo,
  onRedo,
  onCopy,
  onPaste,
  onDuplicate,
  onCommand,
  onCompileScripts,
  onBuildGame,
  projectName = 'MorbiEngine',
  sceneName = 'EmptyScene'
}) => {
  const [activeMenu, setActiveMenu] = React.useState<string | null>(null);
  const menuBarRef = React.useRef<HTMLDivElement>(null);
  const sceneMenuRef = React.useRef<HTMLDivElement>(null);
  const activeDropdownRef = React.useRef<HTMLDivElement>(null);

  React.useLayoutEffect(() => {
    if (activeMenu && activeDropdownRef.current) {
      const rect = activeDropdownRef.current.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      nativeBridge.sendMenuState(true, {
        x: Math.max(0, Math.round((rect.left - 4) * dpr)),
        y: Math.max(0, Math.round((rect.top - 2) * dpr)),
        width: Math.round((rect.width + 8) * dpr),
        height: Math.round((rect.height + 8) * dpr)
      }, 'topbar');
    } else {
      nativeBridge.sendMenuState(false, undefined, 'topbar');
    }
    return () => {
      nativeBridge.sendMenuState(false, undefined, 'topbar');
    };
  }, [activeMenu]);

  React.useEffect(() => {
    if (!activeMenu) return;

    const handleOutsideClick = (e: MouseEvent) => {
      const target = e.target as Node;
      const inMenuBar = menuBarRef.current?.contains(target) ?? false;
      const inSceneMenu = sceneMenuRef.current?.contains(target) ?? false;
      if (!inMenuBar && !inSceneMenu) {
        setActiveMenu(null);
      }
    };

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setActiveMenu(null);
      }
    };

    const handleResize = () => {
      setActiveMenu(null);
    };

    const handleBlur = () => {
      setActiveMenu(null);
    };

    window.addEventListener('pointerdown', handleOutsideClick);
    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('resize', handleResize);
    window.addEventListener('blur', handleBlur);

    return () => {
      window.removeEventListener('pointerdown', handleOutsideClick);
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('resize', handleResize);
      window.removeEventListener('blur', handleBlur);
    };
  }, [activeMenu]);

  const menuItems = ['File', 'Edit', 'View', 'GameObject', 'Component', 'Window', 'Help'];

  return (
    <header className="h-9 bg-[#0b0e14] border-b border-[#1b2230] flex items-center justify-between px-2.5 text-xs select-none z-30">
      {/* Left: Engine Logo + Main Menus */}
      <div className="flex items-center gap-3">
        {/* Engine Brand */}
        <div className="flex items-center gap-2 pr-2 border-r border-[#1e293b]">
          <MorbiLogo className="w-4 h-4 text-indigo-400" />
          <span className="font-semibold text-slate-100 tracking-tight">Morbi Engine</span>
          <span className="text-[10px] text-slate-500 font-mono">v1.0.0 (Dev)</span>
        </div>

        {/* Standard Menu Bar */}
        <nav className="flex items-center gap-0.5" ref={menuBarRef}>
          {menuItems.map((item) => (
            <div key={item} className="relative">
              <button
                onClick={() => setActiveMenu(activeMenu === item ? null : item)}
                aria-haspopup="true"
                aria-expanded={activeMenu === item}
                className={`px-2 py-1 rounded transition-colors text-[11px] ${
                  activeMenu === item
                    ? 'bg-slate-800 text-white'
                    : 'text-slate-300 hover:text-white hover:bg-slate-800/60'
                }`}
              >
                {item}
              </button>

              {/* File Dropdown Menu */}
              {item === 'File' && activeMenu === 'File' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-60 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onNewScene?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>📄</span> New Empty Scene
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+N</span>
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onOpenSceneDialog?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>📂</span> Open Scene...
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+O</span>
                  </button>
                  <div className="h-px bg-[#1f293d] my-1" />
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onSaveScene?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>💾</span> Save Scene
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+S</span>
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onSaveSceneAs?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>📁</span> Save Scene As...
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+Shift+S</span>
                  </button>
                  <div className="h-px bg-[#1f293d] my-1" />
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onImportModel?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>📥</span> Import 3D Model...
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+I</span>
                  </button>
                  <div className="h-px bg-[#1f293d] my-1" />
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCommand?.('reload');
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>⚡</span> Hot Reload Scripts
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">F7</span>
                  </button>
                </div>
              )}

              {/* Edit Dropdown Menu */}
              {item === 'Edit' && activeMenu === 'Edit' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-52 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onUndo?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>Undo</span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+Z</span>
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onRedo?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>Redo</span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+Y</span>
                  </button>
                  <div className="h-px bg-[#1f293d] my-1" />
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCopy?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>Copy Entity</span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+C</span>
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onPaste?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>Paste Entity</span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+V</span>
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onDuplicate?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>Duplicate Entity</span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+D</span>
                  </button>
                  <div className="h-px bg-[#1f293d] my-1" />
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCommand?.('clear');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>🧹</span> Clear Console Logs
                  </button>
                </div>
              )}

              {/* View Dropdown Menu */}
              {item === 'View' && activeMenu === 'View' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-56 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCommand?.('grid');
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>🌐</span> Toggle 3D Ground Grid
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">G</span>
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCommand?.('stat fps');
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>📊</span> Toggle FPS & FrameTime
                    </span>
                  </button>
                </div>
              )}

              {/* GameObject Dropdown Menu */}
              {item === 'GameObject' && activeMenu === 'GameObject' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-56 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCreateEntity?.('EmptyGameObject', 'empty');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>📄</span> Create Empty
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCreateEntity?.('Cube', 'cube');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>🧊</span> 3D Object &gt; Cube
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCreateEntity?.('PointLight', 'light');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>💡</span> Light &gt; Point Light
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onCreateEntity?.('MainCamera', 'camera');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>📷</span> Camera
                  </button>
                  <div className="h-px bg-[#1f293d] my-1" />
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onImportModel?.();
                    }}
                    className="w-full px-3 py-1.5 flex items-center justify-between hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span className="flex items-center gap-2">
                      <span>📥</span> Import 3D Model...
                    </span>
                    <span className="text-[10px] text-slate-400 font-mono">Ctrl+I</span>
                  </button>
                </div>
              )}

              {/* Component Dropdown Menu */}
              {item === 'Component' && activeMenu === 'Component' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-56 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onAddComponent?.('meshRenderer');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>🧊</span> Mesh Renderer
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onAddComponent?.('rigidbody');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>⚡</span> Rigidbody (Jolt 3D)
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onAddComponent?.('light');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>💡</span> Light Component
                  </button>
                  <button
                    onClick={() => {
                      setActiveMenu(null);
                      onAddComponent?.('script');
                    }}
                    className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
                  >
                    <span>📜</span> C# Script (.NET 8)
                  </button>
                </div>
              )}

              {/* Window Dropdown Menu */}
              {item === 'Window' && activeMenu === 'Window' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-48 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <div className="px-3 py-1 text-[10px] text-slate-400 font-mono border-b border-[#1f293d]">
                    PANELS
                  </div>
                  <div className="px-3 py-1 text-xs text-slate-300">✓ Hierarchy Panel</div>
                  <div className="px-3 py-1 text-xs text-slate-300">✓ Inspector Panel</div>
                  <div className="px-3 py-1 text-xs text-slate-300">✓ Asset Browser</div>
                  <div className="px-3 py-1 text-xs text-slate-300">✓ Engine Console</div>
                </div>
              )}

              {/* Help Dropdown Menu */}
              {item === 'Help' && activeMenu === 'Help' && (
                <div ref={activeDropdownRef} className="absolute top-full left-0 mt-1 w-64 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
                  <div className="px-3 py-1.5 text-xs font-semibold text-indigo-400 border-b border-[#1f293d]">
                    MorbiEngine v1.0.0
                  </div>
                  <div className="px-3 py-2 text-[11px] text-slate-300 leading-relaxed">
                    High-Performance C++20 / Vulkan 1.3 Game Engine with .NET 8 CoreCLR and React Edit Mode Viewport.
                  </div>
                  <div className="px-3 py-1 text-[10px] text-slate-500 font-mono border-t border-[#1f293d]">
                    Shortcuts: W: Translate | E: Rotate | R: Scale | Ctrl+P: PIE | RMB: Orbit
                  </div>
                </div>
              )}
            </div>
          ))}
        </nav>
      </div>

      {/* Right / Center Controls */}
      <div className="flex items-center gap-2">
        {/* Quick Save, C# Compile & Build Game Controls */}
        <button
          onClick={onSaveScene}
          title="Save Scene to Disk (Ctrl+S)"
          className="flex items-center gap-1.5 px-2.5 py-1 bg-[#131923] hover:bg-[#1a2332] active:bg-[#222e42] text-slate-200 hover:text-white border border-[#222c3e] hover:border-indigo-500/50 rounded transition-all text-[11px] font-medium shadow-sm group"
        >
          <span className="text-indigo-400 group-hover:text-indigo-300">💾</span>
          <span>Save</span>
          <span className="text-[9px] text-slate-500 font-mono ml-0.5">Ctrl+S</span>
        </button>
        <button
          onClick={onCompileScripts}
          title="Compile C# Scripts via Embedded Roslyn & Hot-Reload"
          className="flex items-center gap-1 px-2 py-1 bg-[#131923] hover:bg-[#1a2332] active:bg-[#222e42] text-slate-200 hover:text-amber-300 border border-[#222c3e] hover:border-amber-500/50 rounded transition-all text-[11px] font-medium shadow-sm"
        >
          <span>⚡</span>
          <span>Scripts</span>
        </button>
        <button
          onClick={onBuildGame}
          title="Build Standalone Game (Compile C++ Launcher + C# Scripts + Chunked MorbiPak)"
          className="flex items-center gap-1 px-2.5 py-1 bg-emerald-900/30 hover:bg-emerald-800/50 active:bg-emerald-700/60 text-emerald-300 hover:text-white border border-emerald-700/50 hover:border-emerald-500 rounded transition-all text-[11px] font-medium shadow-sm"
        >
          <span>📦</span>
          <span>Build</span>
        </button>
        {/* Scene Switcher Dropdown */}
        <div className="relative" ref={sceneMenuRef}>
          <button
            onClick={() => setActiveMenu(activeMenu === 'SceneSwitcher' ? null : 'SceneSwitcher')}
            aria-haspopup="true"
            aria-expanded={activeMenu === 'SceneSwitcher'}
            title={`Active Scene: ${sceneName} (Click to switch scene)`}
            className="flex items-center gap-1.5 px-2.5 py-1 bg-[#131923] hover:bg-[#18212f] border border-[#222c3e] rounded cursor-pointer transition-colors text-slate-200"
          >
            <FolderIcon className="w-3 h-3 text-amber-400" />
            <span className="text-[11px] font-medium">{projectName}</span>
            <span className="text-[10px] text-slate-400 font-mono">/ {sceneName}</span>
            <ChevronDownIcon className="w-2.5 h-2.5 text-slate-400 ml-0.5" />
          </button>
          {activeMenu === 'SceneSwitcher' && (
            <div ref={activeDropdownRef} className="absolute top-full right-0 mt-1 w-60 bg-[#0e131d] border border-[#243044] rounded shadow-2xl py-1 z-50 text-slate-200 backdrop-blur-md">
              <button
                onClick={() => {
                  setActiveMenu(null);
                  onOpenSceneDialog?.();
                }}
                className="w-full px-3 py-1.5 flex items-center gap-2 hover:bg-indigo-600 hover:text-white transition-colors text-left text-xs"
              >
                <span>📂</span>
                <span>Open Scene File...</span>
              </button>
            </div>
          )}
        </div>

        {/* Community & Settings Icons */}
        <div className="flex items-center gap-1 px-1.5 border-l border-r border-[#1e293b] text-slate-400">
          <button
            title="Community Discord"
            className="p-1 hover:text-indigo-400 hover:bg-slate-800/60 rounded transition-colors"
          >
            <DiscordIcon />
          </button>
          <button
            title="GitHub Repository"
            className="p-1 hover:text-white hover:bg-slate-800/60 rounded transition-colors"
          >
            <GithubIcon />
          </button>
          <button
            title="Project & Engine Settings"
            className="p-1 hover:text-sky-400 hover:bg-slate-800/60 rounded transition-colors"
          >
            <SettingsIcon />
          </button>
        </div>

        {/* Unreal Engine Style PIE Transport Controls */}
        <div className="flex items-center gap-1.5 bg-[#10151f] px-1.5 py-1 border border-[#1e293b] rounded shadow-inner">
          {/* Mode Pill Badge */}
          {playMode === 'edit' && (
            <span className="px-1.5 py-0.5 rounded text-[10px] font-mono font-semibold bg-slate-800 text-slate-400 border border-slate-700/60">
              EDIT
            </span>
          )}
          {playMode === 'playing' && (
            <span className="flex items-center gap-1 px-1.5 py-0.5 rounded text-[10px] font-mono font-semibold bg-emerald-950/90 text-emerald-400 border border-emerald-600/60 shadow-sm shadow-emerald-950">
              <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse" />
              PIE
            </span>
          )}
          {playMode === 'paused' && (
            <span className="flex items-center gap-1 px-1.5 py-0.5 rounded text-[10px] font-mono font-semibold bg-amber-950/90 text-amber-400 border border-amber-600/60 shadow-sm shadow-amber-950">
              <span className="w-1.5 h-1.5 rounded-full bg-amber-400" />
              PAUSED
            </span>
          )}

          <div className="h-4 w-px bg-[#1e293b] mx-0.5" />

          {/* Play Button */}
          <button
            onClick={onPlay}
            title={playMode === 'playing' ? 'Simulation Running (PIE)' : 'Play / Simulate Scene (Ctrl+P)'}
            className={`flex items-center justify-center w-6 h-6 rounded transition-all ${
              playMode === 'playing'
                ? 'bg-emerald-600 text-white shadow-sm shadow-emerald-900 ring-1 ring-emerald-400/40'
                : 'bg-emerald-600/20 hover:bg-emerald-600 text-emerald-400 hover:text-white'
            }`}
          >
            <PlayIcon size={12} />
          </button>

          {/* Pause Button */}
          <button
            onClick={onPause}
            disabled={playMode === 'edit'}
            title={playMode === 'paused' ? 'Resume Simulation' : 'Pause Simulation'}
            className={`flex items-center justify-center w-6 h-6 rounded transition-all ${
              playMode === 'paused'
                ? 'bg-amber-600 text-white shadow-sm shadow-amber-900 ring-1 ring-amber-400/40'
                : playMode === 'playing'
                ? 'bg-amber-600/20 hover:bg-amber-600 text-amber-400 hover:text-white'
                : 'text-slate-600 cursor-not-allowed opacity-30'
            }`}
          >
            <PauseIcon size={12} />
          </button>

          {/* Stop Button (Revert to Snapshot) */}
          <button
            onClick={onStop}
            disabled={playMode === 'edit'}
            title="Stop Simulation & Revert to Edit Snapshot (Shift+F5 / Esc)"
            className={`flex items-center justify-center w-6 h-6 rounded transition-all ${
              playMode !== 'edit'
                ? 'bg-rose-600/20 hover:bg-rose-600 text-rose-400 hover:text-white ring-1 ring-rose-500/30'
                : 'text-slate-600 cursor-not-allowed opacity-30'
            }`}
          >
            <StopIcon size={11} />
          </button>

          {/* Step Button (Single Frame Advance) */}
          <button
            onClick={onStep}
            disabled={playMode !== 'paused'}
            title="Step 1 Frame (16.6ms)"
            className={`flex items-center justify-center w-6 h-6 rounded transition-all ${
              playMode === 'paused'
                ? 'bg-indigo-600/30 hover:bg-indigo-600 text-indigo-300 hover:text-white'
                : 'text-slate-600 cursor-not-allowed opacity-30'
            }`}
          >
            <StepIcon size={12} />
          </button>
        </div>

        {/* Window Controls */}
        <div className="flex items-center gap-1 pl-2 text-slate-400">
          <button className="w-5 h-5 flex items-center justify-center hover:bg-slate-800 rounded text-slate-400 hover:text-slate-200">
            &#8211;
          </button>
          <button className="w-5 h-5 flex items-center justify-center hover:bg-slate-800 rounded text-slate-400 hover:text-slate-200 text-[10px]">
            &#9633;
          </button>
          <button className="w-5 h-5 flex items-center justify-center hover:bg-red-600/80 rounded text-slate-400 hover:text-white text-[11px]">
            &#10005;
          </button>
        </div>
      </div>
    </header>
  );
};
