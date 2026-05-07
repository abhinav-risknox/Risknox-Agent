import React from 'react';
import { 
  Shield, 
  Search, 
  Filter, 
  MoreVertical, 
  RefreshCw,
  ExternalLink
} from 'lucide-react';
import { useAgents } from '../hooks/useAgents';
import { cn } from '../lib/utils';
import { Link } from 'react-router-dom';

export const Agents: React.FC = () => {
  const { data, isLoading, isFetching, refetch } = useAgents();

  return (
    <div className="space-y-6 animate-in slide-in-from-bottom-4 duration-500">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-3xl font-display font-bold text-rn-white">Agent Fleet</h1>
          <p className="text-rn-white/40 mt-1">Manage and monitor your endpoints across all environments.</p>
        </div>
        
        <div className="flex items-center gap-3">
          <button 
            onClick={() => refetch()}
            className="p-2.5 rounded-xl bg-rn-white/5 border border-rn-white/5 hover:bg-rn-white/10 transition-all text-rn-white/60 hover:text-rn-white"
          >
            <RefreshCw className={cn("w-5 h-5", isFetching && "animate-spin")} />
          </button>
        </div>
      </div>

      <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl overflow-hidden">
        <div className="p-4 border-b border-rn-white/5 bg-rn-white/[0.01] flex flex-wrap gap-4 items-center justify-between">
          <div className="relative flex-1 min-w-[300px]">
            <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-rn-white/30" />
            <input 
              type="text" 
              placeholder="Filter agents by ID, hostname, or IP..."
              className="w-full bg-rn-black border border-rn-white/10 rounded-xl py-2 pl-10 pr-4 text-sm focus:outline-none focus:ring-1 focus:ring-rn-orange/50 transition-all"
            />
          </div>
          
          <div className="flex items-center gap-2">
            <button className="flex items-center gap-2 px-4 py-2 rounded-xl border border-rn-white/10 text-sm font-bold text-rn-white/60 hover:bg-rn-white/5 hover:text-rn-white transition-all">
              <Filter className="w-4 h-4" />
              Filter
            </button>
            <div className="h-8 w-px bg-rn-white/10 mx-2" />
            <p className="text-xs font-bold text-rn-white/40 uppercase tracking-widest">
              Total: <span className="text-rn-white">{data?.count || 0}</span>
            </p>
          </div>
        </div>

        <div className="overflow-x-auto">
          <table className="w-full text-left">
            <thead>
              <tr className="bg-rn-white/[0.02] text-rn-white/30 text-[10px] uppercase tracking-widest font-bold">
                <th className="px-6 py-4">Status</th>
                <th className="px-6 py-4">Agent Identifier</th>
                <th className="px-6 py-4">Hostname & IP</th>
                <th className="px-6 py-4">Operating System</th>
                <th className="px-6 py-4">Last Seen</th>
                <th className="px-6 py-4 text-right">Actions</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-rn-white/5">
              {data?.agents.map((agent) => (
                <tr key={agent.agent_id} className="hover:bg-rn-white/[0.02] transition-colors group">
                  <td className="px-6 py-4">
                    <div className="flex items-center gap-2">
                      <div className={cn(
                        "w-2 h-2 rounded-full",
                        agent.online ? "bg-green-500 shadow-[0_0_10px_rgba(34,197,94,0.5)]" : "bg-rn-white/10"
                      )} />
                      <span className={cn(
                        "text-xs font-bold uppercase tracking-tight",
                        agent.online ? "text-green-500" : "text-rn-white/20"
                      )}>
                        {agent.online ? 'Online' : 'Offline'}
                      </span>
                    </div>
                  </td>
                  <td className="px-6 py-4">
                    <div className="flex items-center gap-3">
                      <div className="w-10 h-10 rounded-xl bg-rn-white/5 flex items-center justify-center text-rn-white/40 group-hover:bg-rn-orange/10 group-hover:text-rn-orange transition-all">
                        <Shield className="w-5 h-5" />
                      </div>
                      <div>
                        <p className="text-sm font-bold text-rn-white">{agent.agent_id}</p>
                        <p className="text-[10px] text-rn-white/40 font-mono tracking-tighter">VER: {agent.agent_version}</p>
                      </div>
                    </div>
                  </td>
                  <td className="px-6 py-4">
                    <p className="text-sm font-bold text-rn-white">{agent.hostname}</p>
                    <p className="text-xs text-rn-white/40">{agent.ip_address}</p>
                  </td>
                  <td className="px-6 py-4">
                    <p className="text-sm text-rn-white/80">{agent.os_type}</p>
                    <p className="text-xs text-rn-white/40">{agent.os_version}</p>
                  </td>
                  <td className="px-6 py-4">
                    <p className="text-xs text-rn-white/60">{agent.last_seen_at}</p>
                  </td>
                  <td className="px-6 py-4 text-right">
                    <div className="flex items-center justify-end gap-2">
                      <Link 
                        to={`/agents/${agent.agent_id}`}
                        className="p-2 bg-rn-white/5 hover:bg-rn-orange/10 hover:text-rn-orange rounded-lg transition-all"
                        title="Open Control Panel"
                      >
                        <ExternalLink className="w-4 h-4" />
                      </Link>
                      <button className="p-2 text-rn-white/20 hover:text-rn-white">
                        <MoreVertical className="w-4 h-4" />
                      </button>
                    </div>
                  </td>
                </tr>
              ))}
              {isLoading && (
                <tr>
                  <td colSpan={6} className="px-6 py-24 text-center">
                    <RefreshCw className="w-8 h-8 animate-spin mx-auto text-rn-orange/40 mb-4" />
                    <p className="text-rn-white/20 font-bold uppercase tracking-widest text-xs">Fetching Fleet Data...</p>
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
};
