import React from 'react';

interface StatusBarProps {
  statusText?: string;
  projectName?: string;
  platform?: string;
  runtime?: string;
  buildType?: string;
  playMode?: 'edit' | 'playing' | 'paused';
  physicsSummary?: string;
}

export const StatusBar: React.FC<StatusBarProps> = ({
  statusText = 'Ready',
  projectName = 'MorbiSandbox',
  platform = 'Windows',
  runtime = 'x64',
  buildType = 'Release',
  playMode = 'edit',
  physicsSummary
}) => {
  return (
    <footer className="h-6 bg-[#090c12] border-t border-[#19202c] px-2.5 flex items-center justify-between text-[10.5px] select-none text-slate-400 z-30">
      {/* Left Status Indicator */}
      <div className="flex items-center gap-2">
        <span className={`w-2 h-2 rounded-full ${
          playMode === 'playing' ? 'bg-emerald-400 animate-pulse' :
          playMode === 'paused' ? 'bg-amber-400' : 'bg-slate-500'
        }`} />
        <span className="text-slate-200 font-medium">{statusText}</span>
        <span className={`px-1.5 py-0.2 rounded text-[9.5px] font-mono uppercase font-bold ${
          playMode === 'playing' ? 'bg-emerald-950 text-emerald-400 border border-emerald-800/60' :
          playMode === 'paused' ? 'bg-amber-950 text-amber-400 border border-amber-800/60' :
          'bg-slate-800 text-slate-400 border border-slate-700/60'
        }`}>
          {playMode === 'playing' ? 'PIE Active' : (playMode === 'paused' ? 'Paused' : 'Edit Mode')}
        </span>
      </div>

      {/* Right Engine Environment Info */}
      <div className="flex items-center gap-3 font-mono text-[10px]">
        {physicsSummary && (
          <>
            <div className="text-sky-300">{physicsSummary}</div>
            <span className="text-slate-600">|</span>
          </>
        )}
        <div>
          <span className="text-slate-500">Project: </span>
          <span className="text-slate-300 font-sans">{projectName}</span>
        </div>
        <span className="text-slate-600">|</span>
        <div>
          <span className="text-slate-500">Platform: </span>
          <span className="text-slate-300">{platform}</span>
        </div>
        <span className="text-slate-600">|</span>
        <div>
          <span className="text-slate-500">Runtime: </span>
          <span className="text-slate-300">{runtime}</span>
        </div>
        <span className="text-slate-600">|</span>
        <div>
          <span className="text-slate-500">Build: </span>
          <span className="text-indigo-400 font-semibold">{buildType}</span>
        </div>
      </div>
    </footer>
  );
};
