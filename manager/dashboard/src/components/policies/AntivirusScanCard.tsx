import React, { useState } from 'react';
import { ShieldCheck, Search, FolderOpen, RefreshCw, AlertTriangle, CheckCircle2, FileWarning, Bug } from 'lucide-react';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { cn } from '../../lib/utils';

interface AntivirusScanCardProps {
  agentId: string;
  online: boolean;
}

export const AntivirusScanCard: React.FC<AntivirusScanCardProps> = ({ agentId, online }) => {
  const [scanPath, setScanPath] = useState('C:\\Users');
  const queryClient = useQueryClient();

  // Poll status reports for av_scan results
  const { data: statusReports } = useQuery({
    queryKey: ['agent-status', agentId],
    queryFn: () => endpoints.agents.status(agentId).then(res => res.data),
    refetchInterval: 5000,
  });

  // Find the latest av_scan status report
  const avScanReports = statusReports?.reports?.filter(
    (r: any) => r.report_type === 'av_scan'
  ) || [];
  const latestScan = avScanReports.length > 0 ? avScanReports[0] : null;
  const scanData = latestScan?.report_data || null;

  const scanMutation = useMutation({
    mutationFn: (action: string) =>
      endpoints.agents.sendPolicy(agentId, 'antivirus', {
        action,
        path: scanPath,
      }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      queryClient.invalidateQueries({ queryKey: ['agent-status', agentId] });
    },
  });

  const isBusy = scanMutation.isPending;

  return (
    <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 h-full flex flex-col">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-emerald-500/10 flex items-center justify-center text-emerald-500">
            <ShieldCheck className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-lg font-display font-bold text-rn-white">Antivirus Scan</h3>
            <p className="text-xs text-rn-white/40">ClamAV on-demand scan with directory targeting</p>
          </div>
        </div>
        {isBusy && (
          <div className="flex items-center gap-2 px-3 py-1.5 rounded-full bg-rn-orange/10 border border-rn-orange/20">
            <RefreshCw className="w-3 h-3 text-rn-orange animate-spin" />
            <span className="text-[10px] font-bold text-rn-orange uppercase tracking-widest">Dispatching</span>
          </div>
        )}
      </div>

      {/* Scan Path Input */}
      <div className="space-y-3 mb-6">
        <label className="text-[10px] text-rn-white/30 font-bold uppercase tracking-widest flex items-center gap-1.5">
          <FolderOpen className="w-3 h-3" />
          Scan Directory
        </label>
        <div className="flex gap-2">
          <input
            type="text"
            value={scanPath}
            onChange={(e) => setScanPath(e.target.value)}
            placeholder="C:\Users"
            className="flex-1 bg-rn-black border border-rn-white/10 rounded-xl py-2.5 px-4 text-sm font-mono focus:outline-none focus:ring-1 focus:ring-emerald-500/50 focus:border-emerald-500/30 transition-all placeholder:text-rn-white/15"
            disabled={!online || isBusy}
          />
        </div>

        {/* Action Buttons */}
        <div className="flex gap-2">
          <button
            onClick={() => scanMutation.mutate('quick_scan')}
            disabled={!online || isBusy || !scanPath.trim()}
            className={cn(
              "flex-1 flex items-center justify-center gap-2 py-2.5 rounded-xl font-bold text-sm transition-all",
              !online || isBusy
                ? "bg-rn-white/5 text-rn-white/20 cursor-not-allowed"
                : "bg-emerald-500/10 text-emerald-400 border border-emerald-500/20 hover:bg-emerald-500/20 hover:border-emerald-500/40 hover:shadow-[0_0_20px_rgba(16,185,129,0.1)]"
            )}
          >
            <Search className="w-4 h-4" />
            Quick Scan
          </button>
          <button
            onClick={() => {
              setScanPath('C:\\');
              setTimeout(() => scanMutation.mutate('quick_scan'), 50);
            }}
            disabled={!online || isBusy}
            className={cn(
              "flex-1 flex items-center justify-center gap-2 py-2.5 rounded-xl font-bold text-sm transition-all",
              !online || isBusy
                ? "bg-rn-white/5 text-rn-white/20 cursor-not-allowed"
                : "bg-rn-white/5 text-rn-white/60 border border-rn-white/10 hover:bg-rn-orange/10 hover:text-rn-orange hover:border-rn-orange/30"
            )}
          >
            <ShieldCheck className="w-4 h-4" />
            Full System
          </button>
        </div>
      </div>

      {/* Scan Results */}
      <div className="flex-1 min-h-[160px]">
        <div className="text-[10px] text-rn-white/30 font-bold uppercase tracking-widest mb-3">
          Last Scan Result
        </div>

        {!scanData ? (
          <div className="h-full flex flex-col items-center justify-center text-rn-white/10 italic text-sm py-6">
            <ShieldCheck className="w-8 h-8 mb-2 opacity-30" />
            No scan results yet
          </div>
        ) : scanData.type === 'complete' ? (
          <div className="space-y-3">
            {/* Summary Bar */}
            <div className={cn(
              "flex items-center gap-3 p-3.5 rounded-2xl border",
              (scanData.threats || 0) > 0
                ? "bg-red-500/5 border-red-500/20"
                : "bg-emerald-500/5 border-emerald-500/20"
            )}>
              {(scanData.threats || 0) > 0 ? (
                <AlertTriangle className="w-5 h-5 text-red-500 shrink-0" />
              ) : (
                <CheckCircle2 className="w-5 h-5 text-emerald-500 shrink-0" />
              )}
              <div className="flex-1 min-w-0">
                <p className={cn(
                  "text-sm font-bold",
                  (scanData.threats || 0) > 0 ? "text-red-400" : "text-emerald-400"
                )}>
                  {(scanData.threats || 0) > 0
                    ? `${scanData.threats} Threat${scanData.threats > 1 ? 's' : ''} Detected`
                    : 'System Clean'}
                </p>
                <p className="text-[10px] text-rn-white/40 mt-0.5">
                  {scanData.filesScanned?.toLocaleString() || 0} files scanned
                </p>
              </div>
            </div>

            {/* Scan Details */}
            <div className="grid grid-cols-2 gap-2">
              <div className="p-2.5 rounded-xl bg-rn-white/[0.02] border border-rn-white/5">
                <p className="text-[10px] text-rn-white/25 uppercase tracking-wider font-bold">Path</p>
                <p className="text-xs text-rn-white/60 font-mono truncate mt-0.5">{scanData.scanPath || '—'}</p>
              </div>
              <div className="p-2.5 rounded-xl bg-rn-white/[0.02] border border-rn-white/5">
                <p className="text-[10px] text-rn-white/25 uppercase tracking-wider font-bold">Database</p>
                <p className="text-xs text-rn-white/60 font-mono truncate mt-0.5">{scanData.databaseDir?.split('\\').pop() || '—'}</p>
              </div>
            </div>

            {/* Timestamp */}
            {latestScan?.created_at && (
              <p className="text-[10px] text-rn-white/15 font-mono text-right">
                {latestScan.created_at}
              </p>
            )}
          </div>
        ) : scanData.type === 'threat' ? (
          /* Individual threat event */
          <div className="p-3.5 rounded-2xl bg-red-500/5 border border-red-500/20">
            <div className="flex items-center gap-2 mb-2">
              <Bug className="w-4 h-4 text-red-500" />
              <span className="text-xs font-bold text-red-400 uppercase tracking-wider">Threat Detected</span>
            </div>
            <p className="text-xs text-rn-white/60 font-mono break-all">{scanData.file}</p>
            <p className="text-[10px] text-red-400/80 mt-1">{scanData.threat}</p>
          </div>
        ) : scanData.type === 'error' ? (
          <div className="p-3.5 rounded-2xl bg-rn-orange/5 border border-rn-orange/20">
            <div className="flex items-center gap-2 mb-1">
              <FileWarning className="w-4 h-4 text-rn-orange" />
              <span className="text-xs font-bold text-rn-orange">Scan Error</span>
            </div>
            <p className="text-xs text-rn-white/50">{scanData.message || 'Unknown error'}</p>
          </div>
        ) : (
          <pre className="text-[10px] text-rn-white/30 font-mono bg-rn-white/[0.02] p-3 rounded-xl overflow-auto">
            {JSON.stringify(scanData, null, 2)}
          </pre>
        )}
      </div>
    </div>
  );
};
