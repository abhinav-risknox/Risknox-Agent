import React, { useState } from 'react';
import { Globe, Plus, Trash2, ShieldAlert, RefreshCw } from 'lucide-react';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';

interface WebPolicyCardProps {
  agentId: string;
  blockedUrls: any[];
  online: boolean;
}

export const WebPolicyCard: React.FC<WebPolicyCardProps> = ({ agentId, blockedUrls, online }) => {
  const [newUrl, setNewUrl] = useState('');
  const queryClient = useQueryClient();

  const policyMutation = useMutation({
    mutationFn: (data: any) => endpoints.agents.sendPolicy(agentId, 'web_blocking', data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      queryClient.invalidateQueries({ queryKey: ['agent-status', agentId] });
      setNewUrl('');
    }
  });

  const handleAdd = (e: React.FormEvent) => {
    e.preventDefault();
    if (!newUrl) return;
    policyMutation.mutate({ action: 'block', url: newUrl });
  };

  const handleRemove = (url: string) => {
    policyMutation.mutate({ action: 'unblock', url });
  };

  return (
    <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 h-full flex flex-col">
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-blue-500/10 flex items-center justify-center text-blue-500">
            <Globe className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-lg font-display font-bold text-rn-white">Web Blocking</h3>
            <p className="text-xs text-rn-white/40">DNS sinkholing for unauthorized domains</p>
          </div>
        </div>
      </div>

      <form onSubmit={handleAdd} className="flex gap-2 mb-6">
        <input 
          type="text" 
          value={newUrl}
          onChange={(e) => setNewUrl(e.target.value)}
          placeholder="Enter domain (e.g. facebook.com)"
          className="flex-1 bg-rn-black border border-rn-white/10 rounded-xl py-2 px-4 text-sm focus:outline-none focus:ring-1 focus:ring-rn-orange/50 transition-all"
          disabled={!online || policyMutation.isPending}
        />
        <button 
          type="submit"
          disabled={!online || policyMutation.isPending || !newUrl}
          className="p-2.5 rounded-xl bg-rn-orange hover:bg-rn-orange-dim text-white transition-all disabled:opacity-50"
        >
          {policyMutation.isPending ? <RefreshCw className="w-5 h-5 animate-spin" /> : <Plus className="w-5 h-5" />}
        </button>
      </form>

      <div className="flex-1 overflow-y-auto space-y-2 min-h-[200px]">
        {blockedUrls.length === 0 ? (
          <div className="h-full flex flex-col items-center justify-center text-rn-white/10 italic text-sm">
            No domains blocked
          </div>
        ) : (
          blockedUrls.map((item, idx) => (
            <div key={idx} className="flex items-center justify-between p-3 rounded-2xl bg-rn-white/[0.02] border border-rn-white/5 group hover:border-rn-white/10 transition-all">
              <div className="flex items-center gap-3">
                <ShieldAlert className="w-4 h-4 text-rn-white/20 group-hover:text-rn-orange/60 transition-colors" />
                <div>
                  <p className="text-sm font-bold text-rn-white/80">{item.url}</p>
                  <p className="text-[10px] text-rn-white/20 uppercase tracking-widest font-bold">Source: {item.source}</p>
                </div>
              </div>
              <button 
                onClick={() => handleRemove(item.url)}
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
