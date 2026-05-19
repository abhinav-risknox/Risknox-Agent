import React, { useEffect, useMemo, useState } from 'react';
import {
  AlertTriangle,
  CheckCircle2,
  Code2,
  Eye,
  FileText,
  RefreshCw,
  Save,
  ScrollText,
} from 'lucide-react';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { cn } from '../../lib/utils';

interface ConfigPushCardProps {
  agentId: string;
  online: boolean;
}

const DEFAULT_LOG_FORWARDING = {
  logs: [
    {
      name: 'clamscan',
      path: 'C:\\ProgramData\\Risknox Pulse\\antivirus\\clamscan.log',
      channel: 'ClamAV',
    },
    {
      name: 'freshclam',
      path: 'C:\\ProgramData\\Risknox Pulse\\antivirus\\freshclam.log',
      channel: 'ClamAV',
    },
  ],
};

const CONFIG_SECTIONS = [
  { value: 'log_forwarding', label: 'Log Forwarding' },
  { value: 'antivirus', label: 'Antivirus' },
  { value: 'fim', label: 'FIM' },
  { value: 'web_blocking', label: 'Web Blocking' },
  { value: 'software_blocking', label: 'Software Blocking' },
  { value: 'collector', label: 'Collector' },
  { value: 'sender', label: 'Sender' },
  { value: 'all', label: 'All Config' },
  { value: 'custom', label: 'Custom...' },
];

type TrackedCommand = {
  commandId: string;
  verb: 'config_get' | 'config_push';
  requestedAt: number;
};

function formatJson(value: unknown) {
  return JSON.stringify(value, null, 2);
}

function parseResultPayload(payload?: string) {
  if (!payload) return null;
  try {
    return JSON.parse(payload);
  } catch {
    return null;
  }
}

export const ConfigPushCard: React.FC<ConfigPushCardProps> = ({ agentId, online }) => {
  const queryClient = useQueryClient();
  const [section, setSection] = useState('log_forwarding');
  const [sectionChoice, setSectionChoice] = useState('log_forwarding');
  const [editorValue, setEditorValue] = useState(formatJson(DEFAULT_LOG_FORWARDING));
  const [notice, setNotice] = useState<{ type: 'success' | 'error'; text: string } | null>(null);
  const [trackedCommand, setTrackedCommand] = useState<TrackedCommand | null>(null);
  const [lastLoadedAt, setLastLoadedAt] = useState<string | null>(null);
  const [nowMs, setNowMs] = useState(() => Date.now());

  const { data: history } = useQuery({
    queryKey: ['agent-history', agentId],
    queryFn: () => endpoints.commands.listModule({ agent_id: agentId, limit: 25 }).then(res => res.data),
    refetchInterval: 3000,
  });

  const trackedHistoryCommand = useMemo(() => {
    if (!trackedCommand) return null;
    return history?.commands?.find((cmd: any) => cmd.command_id === trackedCommand.commandId) || null;
  }, [history, trackedCommand]);

  const latestConfigPush = useMemo(() => {
    return history?.commands?.find((cmd: any) => cmd.verb === 'config_push' && cmd.status === 'acked');
  }, [history]);

  const configGetPayload = trackedHistoryCommand?.verb === 'config_get'
    ? parseResultPayload(trackedHistoryCommand?.result_payload)
    : null;
  const configPushPayload = parseResultPayload(latestConfigPush?.result_payload);
  const isAllConfig = sectionChoice === 'all';
  const isCustomSection = sectionChoice === 'custom';
  const isWaitingForAgent = !!trackedCommand && !trackedHistoryCommand?.result_payload && trackedHistoryCommand?.status !== 'failed';
  const trackedElapsedSeconds = trackedCommand
    ? Math.floor((nowMs - trackedCommand.requestedAt) / 1000)
    : 0;

  useEffect(() => {
    if (!isWaitingForAgent) return;

    const intervalId = window.setInterval(() => {
      setNowMs(Date.now());
    }, 1000);

    return () => window.clearInterval(intervalId);
  }, [isWaitingForAgent]);

  useEffect(() => {
    if (!configGetPayload?.success) return;
    if (configGetPayload.section === 'all') {
      setSectionChoice('all');
    } else if (configGetPayload.section) {
      const known = CONFIG_SECTIONS.some(item => item.value === configGetPayload.section);
      setSectionChoice(known ? configGetPayload.section : 'custom');
      setSection(configGetPayload.section);
    }
    setEditorValue(formatJson(configGetPayload.config ?? {}));
    setLastLoadedAt(new Date().toLocaleTimeString());
    setNotice({ type: 'success', text: 'Config loaded' });
    setTimeout(() => setNotice(null), 3000);
  }, [trackedHistoryCommand?.command_id, trackedHistoryCommand?.result_payload]);

  useEffect(() => {
    if (!trackedHistoryCommand || trackedHistoryCommand.status !== 'failed') return;
    setNotice({ type: 'error', text: 'Agent command failed' });
    setTimeout(() => setNotice(null), 5000);
  }, [trackedHistoryCommand?.status]);

  const getMutation = useMutation({
    mutationFn: () =>
      endpoints.agents.sendModuleCommand(agentId, 'config_get', {
        section: isAllConfig ? 'all' : section,
      }),
    onSuccess: (response) => {
      setTrackedCommand({
        commandId: response.data.command_id,
        verb: 'config_get',
        requestedAt: Date.now(),
      });
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      setNotice({ type: 'success', text: 'Request sent' });
      setTimeout(() => setNotice(null), 2500);
    },
    onError: () => {
      setNotice({ type: 'error', text: 'Failed to request config' });
      setTimeout(() => setNotice(null), 4000);
    },
  });

  const pushMutation = useMutation({
    mutationFn: (config: unknown) =>
      endpoints.agents.sendModuleCommand(agentId, 'config_push', {
        section,
        config,
      }),
    onSuccess: (response) => {
      setTrackedCommand({
        commandId: response.data.command_id,
        verb: 'config_push',
        requestedAt: Date.now(),
      });
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      setNotice({ type: 'success', text: 'Apply sent' });
      setTimeout(() => setNotice(null), 2500);
    },
    onError: () => {
      setNotice({ type: 'error', text: 'Failed to push config' });
      setTimeout(() => setNotice(null), 4000);
    },
  });

  const loadTemplate = () => {
    setSectionChoice('log_forwarding');
    setSection('log_forwarding');
    setEditorValue(formatJson(DEFAULT_LOG_FORWARDING));
  };

  const applyConfig = () => {
    if (isAllConfig) {
      setNotice({ type: 'error', text: 'Select a section before applying changes' });
      setTimeout(() => setNotice(null), 4000);
      return;
    }

    try {
      const parsed = JSON.parse(editorValue);
      if (!parsed || Array.isArray(parsed) || typeof parsed !== 'object') {
        throw new Error('Config section must be a JSON object');
      }
      pushMutation.mutate(parsed);
    } catch (error) {
      setNotice({
        type: 'error',
        text: error instanceof Error ? error.message : 'Invalid JSON',
      });
      setTimeout(() => setNotice(null), 5000);
    }
  };

  const isBusy = getMutation.isPending || pushMutation.isPending;
  const editorReadOnly = isAllConfig || isWaitingForAgent;

  const handleSectionChoice = (value: string) => {
    setSectionChoice(value);
    if (value === 'all') {
      return;
    }
    if (value !== 'custom') {
      setSection(value);
    }
  };

  return (
    <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 h-full flex flex-col lg:col-span-2">
      <div className="flex items-center justify-between gap-4 mb-6">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-xl bg-sky-500/10 flex items-center justify-center text-sky-500">
            <FileText className="w-5 h-5" />
          </div>
          <div>
            <h3 className="text-lg font-display font-bold text-rn-white">Agent Config</h3>
            <p className="text-xs text-rn-white/40">View current config, edit a section, then apply</p>
          </div>
        </div>

        {notice && (
          <div
            className={cn(
              'flex items-center gap-1.5 px-3 py-1.5 rounded-full border animate-in fade-in duration-300',
              notice.type === 'success'
                ? 'bg-emerald-500/10 border-emerald-500/20 text-emerald-400'
                : 'bg-red-500/10 border-red-500/20 text-red-400'
            )}
          >
            {notice.type === 'success' ? <CheckCircle2 className="w-3 h-3" /> : <AlertTriangle className="w-3 h-3" />}
            <span className="text-[10px] font-bold uppercase tracking-widest">{notice.text}</span>
          </div>
        )}
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-[220px_1fr] gap-4 mb-4">
        <div className="space-y-3">
          <div>
            <label className="text-[10px] text-rn-white/30 font-bold uppercase tracking-widest">Section</label>
            <select
              value={sectionChoice}
              onChange={(event) => handleSectionChoice(event.target.value)}
              disabled={isBusy || isWaitingForAgent}
              className="mt-2 w-full bg-rn-black border border-rn-white/10 rounded-xl py-2.5 px-3 text-sm font-bold text-rn-white/70 focus:outline-none focus:ring-1 focus:ring-sky-500/50 focus:border-sky-500/30 transition-all disabled:opacity-40"
            >
              {CONFIG_SECTIONS.map(item => (
                <option key={item.value} value={item.value}>{item.label}</option>
              ))}
            </select>
            {isCustomSection && (
              <input
                value={section}
                onChange={(event) => setSection(event.target.value)}
                disabled={isBusy || isWaitingForAgent}
                placeholder="custom_section"
                className="mt-2 w-full bg-rn-black border border-rn-white/10 rounded-xl py-2.5 px-3 text-sm font-mono focus:outline-none focus:ring-1 focus:ring-sky-500/50 focus:border-sky-500/30 transition-all disabled:opacity-40"
              />
            )}
          </div>

          <button
            type="button"
            onClick={loadTemplate}
            disabled={isBusy}
            className="w-full flex items-center justify-center gap-2 py-2.5 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/60 hover:text-rn-white hover:bg-rn-white/10 transition-all disabled:opacity-30"
          >
            <ScrollText className="w-4 h-4" />
            Log Template
          </button>
        </div>

        <div className="rounded-2xl border border-rn-white/10 bg-rn-black overflow-hidden">
          <div className="flex items-center justify-between px-4 py-3 border-b border-rn-white/5">
            <div className="flex items-center gap-2 text-rn-white/40">
              <Code2 className="w-4 h-4 text-sky-400" />
              <span className="text-[10px] font-bold uppercase tracking-widest">JSON Editor</span>
            </div>
            <span className="text-[10px] text-rn-white/20 font-mono">
              {isAllConfig ? 'view only' : section}
            </span>
          </div>
          {isWaitingForAgent && (
            <div className="mx-4 mt-4 rounded-xl border border-rn-orange/20 bg-rn-orange/5 px-3 py-2 text-[11px] text-rn-orange/80 font-mono">
              Waiting for agent response ({trackedElapsedSeconds}s)...
            </div>
          )}
          <textarea
            value={editorValue}
            onChange={(event) => setEditorValue(event.target.value)}
            spellCheck={false}
            readOnly={editorReadOnly}
            className={cn(
              'w-full min-h-[260px] bg-transparent p-4 text-[11px] leading-5 text-rn-white/70 font-mono resize-y focus:outline-none',
              editorReadOnly && 'opacity-70'
            )}
          />
        </div>
      </div>

      <div className="flex flex-wrap items-center justify-between gap-3 pt-2">
        <div className="text-[10px] text-rn-white/30 font-mono">
          {configGetPayload?.configPath
            ? `active: ${configGetPayload.configPath}${lastLoadedAt ? ` • loaded ${lastLoadedAt}` : ''}`
            : trackedCommand
              ? `command: ${trackedCommand.commandId.split('-').pop()}`
              : 'request current config to load active values'}
        </div>
        <div className="flex gap-2">
          <button
            type="button"
            onClick={() => getMutation.mutate()}
            disabled={!online || isBusy}
            className="flex items-center gap-2 px-4 py-2.5 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/70 hover:text-rn-white hover:bg-rn-white/10 transition-all disabled:opacity-30"
          >
            {getMutation.isPending ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Eye className="w-4 h-4" />}
            View Current
          </button>
          <button
            type="button"
            onClick={applyConfig}
            disabled={!online || isBusy || isWaitingForAgent || isAllConfig}
            className="flex items-center gap-2 px-4 py-2.5 rounded-xl bg-sky-500/80 hover:bg-sky-500 text-white text-xs font-bold transition-all disabled:opacity-30 disabled:cursor-not-allowed"
          >
            {pushMutation.isPending ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Save className="w-4 h-4" />}
            Apply Section
          </button>
        </div>
      </div>

      {configPushPayload?.success && (
        <div className="mt-4 rounded-2xl border border-emerald-500/10 bg-emerald-500/[0.03] p-3">
          <p className="text-[10px] text-emerald-400 font-bold uppercase tracking-widest mb-2">Last Applied Section</p>
          <pre className="text-[10px] text-rn-white/50 font-mono overflow-auto max-h-[120px]">
            {formatJson(configPushPayload.effectiveConfig)}
          </pre>
        </div>
      )}
    </div>
  );
};
