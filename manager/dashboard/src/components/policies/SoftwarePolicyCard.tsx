import React, { useState } from 'react';
import { LayoutGrid, Plus, Trash2, Cpu, RefreshCw } from 'lucide-react';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { cn } from '../../lib/utils';

interface SoftwarePolicyCardProps {
  agentId: string;
  blockedApps: any[];
  online: boolean;
}

export const SoftwarePolicyCard: React.FC<SoftwarePolicyCardProps> = ({ agentId, blockedApps, online }) => {
  const [appName, setAppName] = useState('');
  const [appExe, setAppExe] = useState('');
  const queryClient = useQueryClient();

  const policyMutation = useMutation({
    mutationFn: (data: any) => endpoints.agents.sendPolicy(agentId, 'software_blocking', data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      queryClient.invalidateQueries({ queryKey: ['agent-status', agentId] });
      setAppName('');
      setAppExe('');
    }
  });

  const handleAdd = (e: React.FormEvent) => {
    e.preventDefault();
    if (!appExe) return;
    policyMutation.mutate({ action: 'block', name: appName || appExe, executable: appExe });
  };

  const handleRemove = (executable: string) => {
    policyMutation.mutate({ action: 'unblock', executable });
  };

  return (
    <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 h-full flex flex-col">
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-purple-500/10 flex items-center justify-center text-purple-500">
            <LayoutGrid className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-lg font-display font-bold text-rn-white">Software Blocking</h3>
            <p className="text-xs text-rn-white/40">Process termination and registry restriction</p>
          </div>
        </div>
      </div>

      <form onSubmit={handleAdd} className="space-y-3 mb-6">
        <input 
          type="text" 
          value={appName}
          onChange={(e) => setAppName(e.target.value)}
          placeholder="App Display Name (Optional)"
          className="w-full bg-rn-black border border-rn-white/10 rounded-xl py-2 px-4 text-sm focus:outline-none focus:ring-1 focus:ring-rn-orange/50 transition-all"
          disabled={!online || policyMutation.isPending}
        />
        <div className="flex gap-2">
          <input 
            type="text" 
            value={appExe}
            onChange={(e) => setAppExe(e.target.value)}
            placeholder="Executable name (e.g. chrome.exe)"
            className="flex-1 bg-rn-black border border-rn-white/10 rounded-xl py-2 px-4 text-sm focus:outline-none focus:ring-1 focus:ring-rn-orange/50 transition-all"
            disabled={!online || policyMutation.isPending}
            required
          />
          <button 
            type="submit"
            disabled={!online || policyMutation.isPending || !appExe}
            className="p-2.5 rounded-xl bg-rn-orange hover:bg-rn-orange-dim text-white transition-all disabled:opacity-50"
          >
            {policyMutation.isPending ? <RefreshCw className="w-5 h-5 animate-spin" /> : <Plus className="w-5 h-5" />}
          </button>
        </div>
      </form>

      <div className="flex-1 overflow-y-auto space-y-2 min-h-[200px]">
        {blockedApps.length === 0 ? (
          <div className="h-full flex flex-col items-center justify-center text-rn-white/10 italic text-sm">
            No applications blocked
          </div>
        ) : (
          blockedApps.map((item, idx) => (
            <div key={idx} className="flex items-center justify-between p-3 rounded-2xl bg-rn-white/[0.02] border border-rn-white/5 group hover:border-rn-white/10 transition-all">
              <div className="flex items-center gap-3">
                <div className="w-8 h-8 rounded-lg bg-rn-white/5 flex items-center justify-center text-rn-white/20 group-hover:text-purple-400 transition-colors">
                  <Cpu className="w-4 h-4" />
                </div>
                <div>
                  <p className="text-sm font-bold text-rn-white/80">{item.name || item.executable}</p>
                  <div className="flex items-center gap-2">
                    <p className="text-[10px] text-rn-white/20 font-mono">{item.executable}</p>
                    <span className="text-[10px] text-rn-orange font-bold uppercase tracking-tighter">Kills: {item.kills}</span>
                  </div>
                </div>
              </div>
              <button 
                onClick={() => handleRemove(item.executable)}
                disabled={!online || policyMutation.isPending}
                className="p-2 text-rn-white/20 hover:text-red-500 hover:bg-red-500/10 rounded-lg transition-all opacity-0 group-hover:opacity-100 disabled:opacity-0"
              >
                <Trash2 className="w-4 h-4" />
              </button>
            </div>
          ))
        )}
      </div>
    </div>
  );
};
