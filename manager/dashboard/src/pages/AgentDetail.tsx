import React, { useState } from 'react';
import { useParams, Link } from 'react-router-dom';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { 
  ChevronLeft, 
  Shield, 
  Terminal, 
  Activity, 
  RefreshCw, 
  Zap, 
  Power,
  Play,
  Square,
  FileCode,
  AlertTriangle,
  CheckCircle2,
  Clock,
  ExternalLink
} from 'lucide-react';
import { endpoints } from '../api/endpoints';
import { cn } from '../lib/utils';
import { WebPolicyCard } from '../components/policies/WebPolicyCard';
import { SoftwarePolicyCard } from '../components/policies/SoftwarePolicyCard';

export const AgentDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const queryClient = useQueryClient();
  const [activeTab, setActiveTab] = useState<'control' | 'policies' | 'history' | 'status'>('control');

  // Queries
  const { data: agent, isLoading: agentLoading } = useQuery({
    queryKey: ['agent', id],
    queryFn: () => endpoints.agents.get(id!).then(res => res.data),
    refetchInterval: 5000,
  });

  const { data: history } = useQuery({
    queryKey: ['agent-history', id],
    queryFn: () => endpoints.commands.listModule({ agent_id: id, limit: 10 }).then(res => res.data),
    refetchInterval: 5000,
  });

  const { data: statusReports } = useQuery({
    queryKey: ['agent-status', id],
    queryFn: () => endpoints.agents.status(id!).then(res => res.data),
    refetchInterval: 5000,
  });

  // Mutation for sending commands
  const commandMutation = useMutation({
    mutationFn: ({ verb, params = {} }: { verb: string, params?: any }) => 
      endpoints.agents.sendModuleCommand(id!, verb, params),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', id] });
    }
  });

  if (agentLoading) {
    return (
      <div className="h-[60vh] flex flex-col items-center justify-center text-rn-white/20">
        <RefreshCw className="w-10 h-10 animate-spin mb-4" />
        <p className="font-bold uppercase tracking-widest text-sm">Initializing Secure Channel...</p>
      </div>
    );
  }

  if (!agent) return <div className="text-white">Agent not found</div>;

  const controlButtons = [
    { label: 'Collector Start', verb: 'collector_start', icon: Play, color: 'green' },
    { label: 'Collector Stop', verb: 'collector_stop', icon: Square, color: 'orange' },
    { label: 'FIM Start', verb: 'fim_start', icon: Shield, color: 'green' },
    { label: 'FIM Stop', verb: 'fim_stop', icon: Shield, color: 'orange' },
    { label: 'Worker Restart', verb: 'worker_restart', icon: RefreshCw, color: 'blue' },
    { label: 'AV Version', verb: 'av_version', icon: Activity, color: 'blue' },
    { label: 'Status Request', verb: 'status_request', icon: Activity, color: 'blue' },
    { label: 'Diagnostics', verb: 'diagnostics', icon: Terminal, color: 'purple', params: { detail: 'full' } },
    { label: 'Agent Restart', verb: 'agent_restart', icon: Power, color: 'orange', danger: true },
  ];

  return (
    <div className="space-y-8 animate-in slide-in-from-right-4 duration-500">
      {/* Header */}
      <div className="flex flex-col md:flex-row md:items-center justify-between gap-6">
        <div className="flex items-center gap-4">
          <Link to="/agents" className="p-2.5 rounded-xl bg-rn-white/5 hover:bg-rn-white/10 transition-all text-rn-white/50 hover:text-rn-white">
            <ChevronLeft className="w-5 h-5" />
          </Link>
          <div className="w-14 h-14 rounded-2xl bg-rn-white/5 border border-rn-white/10 flex items-center justify-center text-rn-orange">
            <Shield className="w-8 h-8" />
          </div>
          <div>
            <div className="flex items-center gap-3">
              <h1 className="text-3xl font-display font-bold text-rn-white">{agent.hostname}</h1>
              <div className={cn(
                "px-2.5 py-0.5 rounded-full text-[10px] font-bold uppercase tracking-widest border",
                agent.online 
                  ? "bg-green-500/10 text-green-500 border-green-500/20 shadow-[0_0_12px_rgba(34,197,94,0.1)]" 
                  : "bg-rn-white/5 text-rn-white/40 border-rn-white/10"
              )}>
                {agent.online ? 'Online' : 'Offline'}
              </div>
            </div>
            <p className="text-rn-white/40 font-mono text-xs mt-1">{agent.agent_id} • {agent.ip_address}</p>
          </div>
        </div>

        <div className="flex gap-3">
          <div className="px-4 py-2 bg-rn-black-card border border-rn-white/5 rounded-xl text-right">
            <p className="text-[10px] text-rn-white/40 uppercase font-bold tracking-widest">OS Environment</p>
            <p className="text-xs font-bold text-rn-white/80">{agent.os_type} {agent.os_version}</p>
          </div>
          <div className="px-4 py-2 bg-rn-black-card border border-rn-white/5 rounded-xl text-right">
            <p className="text-[10px] text-rn-white/40 uppercase font-bold tracking-widest">Agent Version</p>
            <p className="text-xs font-bold text-rn-white/80">{agent.agent_version}</p>
          </div>
        </div>
      </div>

      {/* Tabs */}
      <div className="flex border-b border-rn-white/5">
        {[
          { id: 'control', label: 'Module Control', icon: Zap },
          { id: 'policies', label: 'Security Policies', icon: Shield },
          { id: 'history', label: 'Command History', icon: Clock },
          { id: 'status', label: 'Status Reports', icon: Activity },
        ].map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id as any)}
            className={cn(
              "px-8 py-4 flex items-center gap-2 text-sm font-bold transition-all border-b-2 relative",
              activeTab === tab.id 
                ? "border-rn-orange text-rn-orange" 
                : "border-transparent text-rn-white/40 hover:text-rn-white hover:bg-rn-white/5"
            )}
          >
            <tab.icon className="w-4 h-4" />
            {tab.label}
          </button>
        ))}
      </div>

      {/* Tab Content */}
      <div className="min-h-[400px]">
        {activeTab === 'control' && (
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6 animate-in fade-in slide-in-from-top-2 duration-300">
            {controlButtons.map((btn) => (
              <button
                key={btn.verb}
                onClick={() => commandMutation.mutate({ verb: btn.verb, params: btn.params })}
                disabled={!agent.online || commandMutation.isPending}
                className={cn(
                  "flex flex-col items-start p-6 rounded-3xl border transition-all text-left group",
                  !agent.online 
                    ? "bg-rn-white/5 border-rn-white/5 opacity-50 cursor-not-allowed" 
                    : btn.danger
                      ? "bg-rn-white/5 border-rn-white/10 hover:border-red-500/50 hover:bg-red-500/5"
                      : "bg-rn-white/5 border-rn-white/10 hover:border-rn-orange/50 hover:bg-rn-orange/5"
                )}
              >
                <div className={cn(
                  "w-12 h-12 rounded-2xl flex items-center justify-center mb-4 transition-all",
                  !agent.online ? "bg-rn-white/5 text-rn-white/20" : "bg-rn-white/5 text-rn-white/40 group-hover:bg-rn-white/10 group-hover:text-rn-orange"
                )}>
                  <btn.icon className="w-6 h-6" />
                </div>
                <h3 className="text-lg font-display font-bold text-rn-white">{btn.label}</h3>
                <p className="text-xs text-rn-white/40 mt-1 uppercase tracking-widest font-mono">{btn.verb}</p>
                {!agent.online && <p className="text-[10px] text-rn-orange mt-4 font-bold">Offline: Command Buffer Only</p>}
              </button>
            ))}
          </div>
        )}

        {activeTab === 'policies' && (
          <div className="space-y-6 animate-in fade-in slide-in-from-top-2 duration-300">
            <div className="flex items-center justify-between mb-2">
              <h2 className="text-sm font-bold text-rn-white/40 uppercase tracking-[0.2em]">Active Policy State</h2>
              <button 
                onClick={() => commandMutation.mutate({ verb: 'status_request' })}
                disabled={!agent.online || commandMutation.isPending}
                className="flex items-center gap-2 px-4 py-2 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/60 hover:text-rn-white hover:bg-rn-white/10 transition-all"
              >
                <RefreshCw className={cn("w-3.5 h-3.5", commandMutation.isPending && "animate-spin")} />
                Refresh from Agent
              </button>
            </div>
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
              <WebPolicyCard 
                agentId={id!} 
                blockedUrls={statusReports?.reports.find((r: any) => r.report_type === 'module_status')?.report_data?.web_blocking?.blockedUrls || []} 
                online={agent.online} 
              />
              <SoftwarePolicyCard 
                agentId={id!} 
                blockedApps={statusReports?.reports.find((r: any) => r.report_type === 'module_status')?.report_data?.software_blocking?.blockedApps || []} 
                online={agent.online} 
              />
            </div>
          </div>
        )}

        {activeTab === 'history' && (
          <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl overflow-hidden animate-in fade-in duration-300">
            <table className="w-full text-left">
              <thead>
                <tr className="bg-rn-white/[0.02] text-rn-white/30 text-[10px] uppercase tracking-widest font-bold">
                  <th className="px-6 py-4">ID</th>
                  <th className="px-6 py-4">Command Verb</th>
                  <th className="px-6 py-4">Parameters</th>
                  <th className="px-6 py-4">Status</th>
                  <th className="px-6 py-4">Timestamp</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-rn-white/5">
                {history?.commands.map((cmd: any) => (
                  <tr key={cmd.command_id} className="hover:bg-rn-white/[0.02] transition-colors">
                    <td className="px-6 py-4">
                      <span className="text-[10px] font-mono text-rn-white/40">{cmd.command_id.split('-').pop()}</span>
                    </td>
                    <td className="px-6 py-4">
                      <div className="flex items-center gap-2">
                        <Terminal className="w-3.5 h-3.5 text-rn-orange/60" />
                        <span className="text-sm font-bold text-rn-white">{cmd.verb}</span>
                      </div>
                    </td>
                    <td className="px-6 py-4">
                      <pre className="text-[10px] text-rn-white/40 overflow-hidden text-ellipsis whitespace-nowrap max-w-[200px]">
                        {JSON.stringify(cmd.params)}
                      </pre>
                    </td>
                    <td className="px-6 py-4">
                      <div className="flex items-center gap-2">
                        {cmd.status === 'acked' ? (
                          <>
                            <CheckCircle2 className="w-4 h-4 text-green-500" />
                            <span className="text-xs text-green-500 font-bold uppercase">Acknowledged</span>
                          </>
                        ) : cmd.status === 'failed' ? (
                          <>
                            <AlertTriangle className="w-4 h-4 text-rn-orange" />
                            <span className="text-xs text-rn-orange font-bold uppercase">Failed</span>
                          </>
                        ) : (
                          <>
                            <RefreshCw className="w-3.5 h-3.5 text-rn-white/20 animate-spin" />
                            <span className="text-xs text-rn-white/40 font-bold uppercase">Pending</span>
                          </>
                        )}
                      </div>
                    </td>
                    <td className="px-6 py-4">
                      <span className="text-xs text-rn-white/40">{cmd.created_at}</span>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {activeTab === 'status' && (
          <div className="space-y-6 animate-in fade-in duration-300">
            {statusReports?.reports.map((report: any, idx: number) => (
              <div key={idx} className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6">
                <div className="flex items-center justify-between mb-4 pb-4 border-b border-rn-white/5">
                  <div className="flex items-center gap-3">
                    <div className="w-8 h-8 rounded-lg bg-rn-white/5 flex items-center justify-center text-rn-orange">
                      <FileCode className="w-5 h-5" />
                    </div>
                    <div>
                      <h3 className="text-sm font-bold text-rn-white uppercase tracking-wider">{report.report_type}</h3>
                      <p className="text-[10px] text-rn-white/40">{report.created_at}</p>
                    </div>
                  </div>
                  <button className="text-rn-white/20 hover:text-rn-white transition-all">
                    <ExternalLink className="w-4 h-4" />
                  </button>
                </div>
                <pre className="bg-rn-black p-4 rounded-2xl text-[11px] text-rn-white/60 font-mono overflow-x-auto border border-rn-white/5">
                  {JSON.stringify(report.report_data, null, 2)}
                </pre>
              </div>
            ))}
            {(!statusReports || statusReports.reports.length === 0) && (
              <div className="p-12 text-center text-rn-white/20 font-bold uppercase tracking-widest text-xs border-2 border-dashed border-rn-white/5 rounded-3xl">
                No health reports available for this endpoint
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
};
