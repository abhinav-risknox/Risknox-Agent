import React from 'react';
import { 
  Users, 
  Activity, 
  ShieldCheck, 
  Zap, 
  ArrowUpRight,
  MoreVertical,
  Plus,
  RefreshCw
} from 'lucide-react';
import { StatCard } from '../components/dashboard/StatCard';
import { Breadcrumb } from '../components/common/Breadcrumb';
import { useAgents } from '../hooks/useAgents';
import { cn } from '../lib/utils';
import { Link } from 'react-router-dom';

export const Dashboard: React.FC = () => {
  const { data, isLoading, isError, refetch, isFetching } = useAgents();

  const totalAgents = data?.count || 0;
  const onlineAgents = data?.agents.filter(a => a.online).length || 0;
  
  // Mock data for trends/threats for UI demo
  const threatsBlocked = 42; 
  const commandsSent = 2401;

  return (
    <div className="animate-in fade-in duration-700">
      <Breadcrumb title="Dashboard" pageTitle="Overview" />

      <div className="flex items-center justify-between mb-8 bg-rn-black-card/40 p-6 rounded-2xl border border-rn-white/5">
        <div>
          <h1 className="text-2xl font-display font-bold text-rn-white">Security Posture</h1>
          <p className="text-rn-white/40 text-sm mt-1">Live monitoring and control of your distributed agent network.</p>
        </div>
        
        <div className="flex items-center gap-4">
          <button 
            onClick={() => refetch()}
            disabled={isFetching}
            className="flex items-center gap-2 py-2 px-4 rounded-lg bg-rn-white/5 border border-rn-white/5 hover:bg-rn-white/10 transition-all text-rn-white/60 hover:text-rn-white text-xs font-bold uppercase tracking-wider"
          >
            <RefreshCw className={cn("w-4 h-4", isFetching && "animate-spin")} />
            Refresh
          </button>
          <button className="bg-rn-orange hover:bg-rn-orange-dim text-white font-bold py-2.5 px-6 rounded-xl flex items-center gap-2 transition-all shadow-lg shadow-rn-orange/20 hover:scale-[1.02] active:scale-[0.98]">
            <Plus className="w-5 h-5" />
            Deploy Agent
          </button>
        </div>
      </div>

      {/* Stats Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6 mb-10">
        <StatCard 
          title="Total Agents" 
          value={isLoading ? "..." : totalAgents} 
          icon={Users} 
          trend="12%" 
          trendUp={true} 
          color="blue"
        />
        <StatCard 
          title="Active Sessions" 
          value={isLoading ? "..." : onlineAgents} 
          icon={Activity} 
          trend="5%" 
          trendUp={true} 
          color="green"
        />
        <StatCard 
          title="Threats Blocked" 
          value={threatsBlocked} 
          icon={ShieldCheck} 
          trend="24%" 
          trendUp={false} 
          color="orange"
        />
        <StatCard 
          title="Commands Sent" 
          value={commandsSent} 
          icon={Zap} 
          trend="8%" 
          trendUp={true} 
          color="purple"
        />
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-8">
        {/* Main Agent Table Preview */}
        <div className="lg:col-span-2 bg-rn-black-card/40 border border-rn-white/5 rounded-2xl overflow-hidden flex flex-col">
          <div className="p-6 border-b border-rn-white/5 flex items-center justify-between bg-rn-white/[0.02]">
            <h2 className="text-sm font-bold text-rn-white uppercase tracking-wider">Connected Agents</h2>
            <Link to="/agents" className="text-rn-orange text-xs font-bold flex items-center gap-1 hover:underline px-3 py-1 bg-rn-orange/10 rounded-lg transition-all">
              View All <ArrowUpRight className="w-3 h-3" />
            </Link>
          </div>
          
          <div className="overflow-x-auto flex-1">
            {isLoading ? (
              <div className="p-12 flex flex-col items-center justify-center text-rn-white/20">
                <RefreshCw className="w-8 h-8 animate-spin mb-4" />
                <p className="font-bold uppercase tracking-widest text-xs">Loading Agents...</p>
              </div>
            ) : isError ? (
              <div className="p-12 flex flex-col items-center justify-center text-rn-orange/40">
                <ShieldCheck className="w-8 h-8 mb-4" />
                <p className="font-bold uppercase tracking-widest text-xs">Connection Error</p>
                <p className="text-sm mt-2">Could not reach the Manager API.</p>
              </div>
            ) : (
              <table className="w-full text-left">
                <thead>
                  <tr className="bg-rn-white/[0.02] text-rn-white/30 text-[10px] uppercase tracking-widest font-bold">
                    <th className="px-6 py-4">Agent ID</th>
                    <th className="px-6 py-4">Hostname</th>
                    <th className="px-6 py-4">System</th>
                    <th className="px-6 py-4">Status</th>
                    <th className="px-6 py-4">Actions</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-rn-white/5">
                  {data?.agents.slice(0, 5).map((agent) => (
                    <tr key={agent.agent_id} className="hover:bg-rn-white/[0.02] transition-colors group">
                      <td className="px-6 py-4">
                        <span className="text-sm font-mono text-rn-white/60">{agent.agent_id}</span>
                      </td>
                      <td className="px-6 py-4">
                        <span className="text-sm font-bold text-rn-white">{agent.hostname}</span>
                      </td>
                      <td className="px-6 py-4">
                        <div className="flex flex-col">
                          <span className="text-xs text-rn-white/80">{agent.os_type}</span>
                          <span className="text-[10px] text-rn-white/40">{agent.os_version}</span>
                        </div>
                      </td>
                      <td className="px-6 py-4">
                        <div className="flex items-center gap-2">
                          <div className={cn(
                            "w-1.5 h-1.5 rounded-full shadow-[0_0_8px_rgba(34,197,94,0.4)]",
                            agent.online ? "bg-green-500 animate-pulse" : "bg-rn-white/20"
                          )} />
                          <span className={cn(
                            "text-xs font-medium",
                            agent.online ? "text-green-500" : "text-rn-white/40"
                          )}>
                            {agent.online ? 'Online' : 'Offline'}
                          </span>
                        </div>
                      </td>
                      <td className="px-6 py-4">
                        <Link 
                          to={`/agents/${agent.agent_id}`}
                          className="p-2 inline-block text-rn-white/20 hover:text-rn-white transition-colors"
                        >
                          <MoreVertical className="w-4 h-4" />
                        </Link>
                      </td>
                    </tr>
                  ))}
                  {data?.agents.length === 0 && (
                    <tr>
                      <td colSpan={5} className="px-6 py-12 text-center text-rn-white/20 font-bold uppercase tracking-widest text-xs">
                        No agents registered
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            )}
          </div>
        </div>

        {/* Recent Activity Sidebar - Static for now */}
        <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-8">
          <h2 className="text-xl font-display font-bold text-rn-white mb-6">Recent Activity</h2>
          
          <div className="space-y-6">
            {[1, 2, 3, 4].map((i) => (
              <div key={i} className="flex gap-4 relative">
                {i < 4 && <div className="absolute left-[11px] top-7 bottom-[-15px] w-[2px] bg-rn-white/5" />}
                <div className="w-6 h-6 rounded-full bg-rn-orange/20 border border-rn-orange/30 flex items-center justify-center shrink-0 z-10">
                  <div className="w-2 h-2 rounded-full bg-rn-orange" />
                </div>
                <div>
                  <p className="text-sm font-bold text-rn-white">Module Control</p>
                  <p className="text-xs text-rn-white/40 mt-0.5">Command status_request sent to agent cluster</p>
                  <p className="text-[10px] text-rn-white/20 mt-2 font-bold uppercase tracking-wider">Just now</p>
                </div>
              </div>
            ))}
          </div>
          
          <Link 
            to="/audit"
            className="w-full mt-8 py-3 inline-block text-center rounded-xl bg-rn-white/5 text-rn-white/60 text-sm font-bold hover:bg-rn-white/10 hover:text-rn-white transition-all"
          >
            View Audit Log
          </Link>
        </div>
      </div>
    </div>
  );
};
