import React, { useState, useEffect, useRef } from 'react';
import {
  TerminalIcon,
  ProfilerIcon,
  BugIcon,
  DockIcon,
  ChevronDownIcon
} from './Icons';
import { ConsoleLogMessage } from '../types';

interface ConsolePanelProps {
  logs?: ConsoleLogMessage[];
  onExecuteCommand?: (cmd: string) => void;
}

export const ConsolePanel: React.FC<ConsolePanelProps> = ({
  logs = [],
  onExecuteCommand
}) => {
  const [activeTab, setActiveTab] = useState<'Console' | 'Profiler' | 'Debug'>('Console');
  const [filterLevel, setFilterLevel] = useState<'All' | 'Info' | 'Warning' | 'Error' | 'Debug'>('All');
  const [commandInput, setCommandInput] = useState('');
  const logContainerRef = useRef<HTMLDivElement>(null);

  // Auto-scroll to bottom on new log
  useEffect(() => {
    if (logContainerRef.current) {
      logContainerRef.current.scrollTop = logContainerRef.current.scrollHeight;
    }
  }, [logs]);

  const infoCount = logs.filter((l) => l.level === 'info').length;
  const warnCount = logs.filter((l) => l.level === 'warning').length;
  const errCount = logs.filter((l) => l.level === 'error').length;

  const handleCommandSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!commandInput.trim()) return;
    onExecuteCommand?.(commandInput.trim());
    setCommandInput('');
  };

  const getLevelBadge = (level: string) => {
    switch (level) {
      case 'info':
        return (
          <span className="flex items-center gap-1 text-sky-400 shrink-0">
            <span className="w-1.5 h-1.5 rounded-full bg-sky-400" />
            <span className="font-semibold">[Info]</span>
          </span>
        );
      case 'warning':
        return (
          <span className="flex items-center gap-1 text-amber-400 shrink-0">
            <span className="w-1.5 h-1.5 rounded-full bg-amber-400" />
            <span className="font-semibold">[Warning]</span>
          </span>
        );
      case 'error':
        return (
          <span className="flex items-center gap-1 text-red-400 shrink-0">
            <span className="w-1.5 h-1.5 rounded-full bg-red-400" />
            <span className="font-semibold">[Error]</span>
          </span>
        );
      case 'debug':
        return (
          <span className="flex items-center gap-1 text-purple-400 shrink-0">
            <span className="w-1.5 h-1.5 rounded-full bg-purple-400" />
            <span className="font-semibold">[Debug]</span>
          </span>
        );
      default:
        return null;
    }
  };

  const filteredLogs = logs.filter((l) =>
    filterLevel === 'All' ? true : l.level === filterLevel.toLowerCase()
  );

  return (
    <div className="w-[430px] bg-[#0c1017] flex flex-col select-none overflow-hidden shrink-0">
      {/* Tab Header */}
      <div className="h-8 px-2 bg-[#090c12] border-b border-[#19202c] flex items-center justify-between text-[11px]">
        <div className="flex items-center gap-1">
          <button
            onClick={() => setActiveTab('Console')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors font-medium ${
              activeTab === 'Console'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <TerminalIcon size={12} />
            <span>Console ({logs.length})</span>
          </button>
          <button
            onClick={() => setActiveTab('Profiler')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors ${
              activeTab === 'Profiler'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <ProfilerIcon size={12} />
            <span>Profiler</span>
          </button>
          <button
            onClick={() => setActiveTab('Debug')}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded transition-colors ${
              activeTab === 'Debug'
                ? 'bg-indigo-600 text-white'
                : 'text-slate-400 hover:text-slate-200 hover:bg-[#141b27]'
            }`}
          >
            <BugIcon size={12} />
            <span>Debug</span>
          </button>
        </div>

        <button className="p-1 text-slate-400 hover:text-white rounded hover:bg-slate-800">
          <DockIcon size={11} />
        </button>
      </div>

      {/* Filter Toolbar */}
      <div className="h-7 px-2 border-b border-[#19202c] flex items-center justify-between text-[10px] text-slate-400">
        <div className="flex items-center gap-1">
          <span className="text-slate-500">Filter:</span>
          <button
            onClick={() =>
              setFilterLevel(
                filterLevel === 'All'
                  ? 'Info'
                  : filterLevel === 'Info'
                  ? 'Warning'
                  : filterLevel === 'Warning'
                  ? 'Error'
                  : 'All'
              )
            }
            className="flex items-center gap-1 px-1.5 py-0.5 bg-[#121822] border border-[#1e2738] rounded text-slate-300"
          >
            <span>{filterLevel} Messages</span>
            <ChevronDownIcon size={9} />
          </button>
        </div>
        <div className="flex items-center gap-2 font-mono text-[9.5px]">
          <span className="text-sky-400">Info: {infoCount}</span>
          <span className="text-amber-400">Warn: {warnCount}</span>
          <span className="text-red-400">Err: {errCount}</span>
        </div>
      </div>

      {/* Log Output Stream */}
      <div
        ref={logContainerRef}
        className="flex-1 p-2 overflow-y-auto font-mono text-[10px] leading-relaxed space-y-0.5 scrollbar-thin"
      >
        {filteredLogs.length === 0 ? (
          <div className="py-6 text-center text-slate-500 italic">No logs recorded</div>
        ) : (
          filteredLogs.map((log) => (
            <div
              key={log.id}
              className="flex items-start gap-1.5 py-0.5 hover:bg-[#131b27] px-1 rounded transition-colors text-slate-300"
            >
              <span className="text-slate-600 text-[9px] shrink-0 font-mono">{log.timestamp}</span>
              {getLevelBadge(log.level)}
              <span className="break-all flex-1">{log.message}</span>
            </div>
          ))
        )}
      </div>

      {/* Command Line Input Prompt */}
      <form
        onSubmit={handleCommandSubmit}
        className="h-7 border-t border-[#19202c] bg-[#090c12] flex items-center px-2 gap-1.5"
      >
        <span className="text-indigo-400 font-mono text-xs font-bold">&gt;</span>
        <input
          type="text"
          value={commandInput}
          onChange={(e) => setCommandInput(e.target.value)}
          placeholder="Execute engine command (e.g., stat fps, reload)..."
          className="flex-1 bg-transparent text-slate-200 placeholder-slate-600 text-[11px] font-mono focus:outline-none"
        />
      </form>
    </div>
  );
};
