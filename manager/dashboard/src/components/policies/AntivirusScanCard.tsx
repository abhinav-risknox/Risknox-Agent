import React, { useState, useEffect, useRef } from 'react';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { Card, CardBody, CardHeader, Input, Button, InputGroup } from 'reactstrap';

interface AntivirusScanCardProps {
  agentId: string;
  online: boolean;
}

type ScanPhase = 'idle' | 'sending' | 'queued' | 'scanning' | 'complete' | 'error';

interface ScanResult {
  filesScanned: number;
  threats: number;
  scanPath: string;
  threatList: Array<{ file: string; threat: string }>;
}

interface ScanError {
  message: string;
  exitCode?: number;
  details?: string[];
}

// Animated indeterminate progress bar using inline styles (no extra deps)
const IndeterminateBar: React.FC = () => {
  return (
    <div
      style={{
        position: 'relative',
        height: '6px',
        borderRadius: '3px',
        background: 'rgba(99,102,241,0.15)',
        overflow: 'hidden',
        width: '100%',
      }}
    >
      <style>{`
        @keyframes av-slide {
          0%   { left: -45%; width: 45%; }
          50%  { left: 55%;  width: 45%; }
          100% { left: 125%; width: 10%; }
        }
        .av-bar-inner {
          position: absolute;
          height: 100%;
          border-radius: 3px;
          background: linear-gradient(90deg, #6366f1, #818cf8);
          animation: av-slide 1.4s cubic-bezier(0.4,0,0.2,1) infinite;
        }
      `}</style>
      <div className="av-bar-inner" />
    </div>
  );
};

const ElapsedTimer: React.FC<{ startTime: number }> = ({ startTime }) => {
  const [elapsed, setElapsed] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setElapsed(Math.floor((Date.now() - startTime) / 1000)), 1000);
    return () => clearInterval(id);
  }, [startTime]);
  const m = Math.floor(elapsed / 60);
  const s = elapsed % 60;
  return <span className="font-monospace fs-11 text-muted">{m > 0 ? `${m}m ` : ''}{s}s</span>;
};

export const AntivirusScanCard: React.FC<AntivirusScanCardProps> = ({ agentId, online }) => {
  const [targetPath, setTargetPath] = useState('C:\\');
  const [scanType, setScanType] = useState<'quick' | 'full' | 'custom'>('quick');
  const [scanPhase, setScanPhase] = useState<ScanPhase>('idle');
  const [commandId, setCommandId] = useState<string | null>(null);
  const [scanResult, setScanResult] = useState<ScanResult | null>(null);
  const [scanError, setScanError] = useState<ScanError | null>(null);
  const [scanStartTime, setScanStartTime] = useState<number>(0);
  // Timestamp when the scan was initiated — used to filter stale av_scan reports
  const scanInitRef = useRef<string>('');

  const queryClient = useQueryClient();

  // ── Main status reports query (already exists in AgentDetail, deduplicated by React Query) ──
  const { data: statusReports } = useQuery({
    queryKey: ['agent-status', agentId],
    queryFn: () => endpoints.agents.status(agentId).then(res => res.data),
    refetchInterval: 5000,
  });

  const { data: cmdData } = useQuery({
    queryKey: ['cmd-status', commandId],
    queryFn: () => endpoints.commands.get(commandId!).then(res => res.data),
    enabled: !!commandId && scanPhase === 'queued',
    refetchInterval: 3000,
  });

  // Watch command status: queued → scanning / error
  useEffect(() => {
    if (!cmdData || scanPhase !== 'queued') return;
    const status = (cmdData as any).status;
    if (status === 'acked') {
      setScanPhase('scanning');
    } else if (status === 'failed') {
      setScanError({ message: `Command failed: ${(cmdData as any).ack_status || 'unknown'}` });
      setScanPhase('error');
    }
  }, [cmdData, scanPhase]);

  // Watch av_scan status reports: scanning → complete / error
  useEffect(() => {
    if (scanPhase !== 'scanning' && scanPhase !== 'queued') return;
    if (!statusReports?.reports) return;

    const avReports: any[] = statusReports.reports.filter(
      (r: any) => r.report_type === 'av_scan'
    );
    if (avReports.length === 0) return;

    // Most recent av_scan report
    const latest = avReports.sort(
      (a: any, b: any) => new Date(b.created_at).getTime() - new Date(a.created_at).getTime()
    )[0];

    // Ignore stale reports that predate this scan.
    // NOTE: Postgres returns created_at as "YYYY-MM-DD HH:MM:SS" (space, no T/Z),
    // so we must parse both as Date objects — string comparison would always fail.
    if (scanInitRef.current) {
      const reportTime = new Date(latest.created_at).getTime();
      const scanInitTime = new Date(scanInitRef.current).getTime();
      if (!isNaN(reportTime) && !isNaN(scanInitTime) && reportTime <= scanInitTime) return;
    }

    const d = latest.report_data || {};
    const type = d.type || '';

    if (type === 'complete') {
      // Build threat list from accumulated threat events if available
      const threatList: Array<{ file: string; threat: string }> = Array.isArray(d.threatList)
        ? d.threatList
        : [];

      setScanResult({
        filesScanned: d.filesScanned ?? 0,
        threats: d.threats ?? 0,
        scanPath: d.scanPath ?? '',
        threatList,
      });
      setScanPhase('complete');
    } else if (type === 'error') {
      setScanError({
        message: d.message || 'Scan failed',
        exitCode: d.exitCode,
        details: d.details,
      });
      setScanPhase('error');
    }
  }, [statusReports, scanPhase]);

  // ── Mutation ──
  const commandMutation = useMutation({
    mutationFn: (data: any) => endpoints.agents.sendPolicy(agentId, 'antivirus', data),
    onMutate: () => {
      setScanPhase('sending');
      setScanResult(null);
      setScanError(null);
      setScanStartTime(Date.now());
      // Record the time just before we send so we can filter stale reports
      scanInitRef.current = new Date().toISOString();
    },
    onSuccess: (res: any) => {
      const cid = res?.data?.command_id ?? null;
      setCommandId(cid);
      setScanPhase('queued');
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
    },
    onError: (err: any) => {
      setScanError({
        message: err?.response?.data?.error || err?.message || 'Failed to send command',
      });
      setScanPhase('error');
    },
  });

  const handleStartScan = () => {
    commandMutation.mutate({
      action: 'start',
      type: scanType,
      path: scanType === 'custom' ? targetPath : undefined,
    });
  };

  const handleReset = () => {
    setScanPhase('idle');
    setScanResult(null);
    setScanError(null);
    setCommandId(null);
  };

  // ── Derive last known scan from module_status for when we're idle ──
  const avStatusReport = statusReports?.reports?.find((r: any) => r.report_type === 'module_status');
  const avState = avStatusReport?.report_data?.antivirus || {};

  // ── Phase-aware header badge ──
  const headerBadge = () => {
    switch (scanPhase) {
      case 'sending':
        return <span className="badge bg-secondary">Sending...</span>;
      case 'queued':
        return (
          <span className="badge" style={{ background: '#f59e0b', color: '#fff' }}>
            Queued
          </span>
        );
      case 'scanning':
        return (
          <span className="badge" style={{ background: '#6366f1', color: '#fff' }}>
            Scanning...
          </span>
        );
      case 'complete':
        return scanResult && scanResult.threats > 0
          ? <span className="badge bg-danger">Threats Found</span>
          : <span className="badge bg-success">Clean</span>;
      case 'error':
        return <span className="badge bg-warning text-dark">Error</span>;
      default:
        return <span className="badge bg-success-subtle text-success">Idle</span>;
    }
  };

  const isActive = scanPhase === 'sending' || scanPhase === 'queued' || scanPhase === 'scanning';

  return (
    <Card className="h-100 mb-0">
      <CardHeader className="d-flex align-items-center border-0 pt-4 pb-0 px-4">
        <div className="d-flex align-items-center gap-3 flex-grow-1">
          <div className="avatar-sm flex-shrink-0">
            <div
              className="avatar-title rounded fs-18"
              style={{
                background:
                  scanPhase === 'error'
                    ? 'rgba(245,158,11,0.15)'
                    : scanResult?.threats
                    ? 'rgba(239,68,68,0.12)'
                    : 'rgba(99,102,241,0.1)',
                color:
                  scanPhase === 'error'
                    ? '#f59e0b'
                    : scanResult?.threats
                    ? '#ef4444'
                    : '#6366f1',
              }}
            >
              <i
                className={
                  scanPhase === 'error'
                    ? 'ri-error-warning-line fs-20'
                    : scanResult?.threats
                    ? 'ri-bug-2-line fs-20'
                    : 'ri-shield-check-line fs-20'
                }
              />
            </div>
          </div>
          <div>
            <h6 className="fs-15 fw-bold mb-1">Antivirus Scanner</h6>
            <div className="d-flex align-items-center gap-2">
              {headerBadge()}
              {avState.definitions && (
                <p className="text-muted fs-11 font-monospace mb-0">
                  Defs: {avState.definitions}
                </p>
              )}
            </div>
          </div>
        </div>
        {isActive && scanStartTime > 0 && (
          <div className="flex-shrink-0">
            <ElapsedTimer startTime={scanStartTime} />
          </div>
        )}
      </CardHeader>

      <CardBody className="p-4 d-flex flex-column">
        {/* ── Scan type selector — hidden while active or showing results ── */}
        {scanPhase === 'idle' && (
          <div className="mb-4">
            <div className="d-flex gap-2 mb-3">
              {(['quick', 'full', 'custom'] as const).map(type => (
                <Button
                  key={type}
                  color={scanType === type ? 'primary' : 'light'}
                  onClick={() => setScanType(type)}
                  className="flex-grow-1 text-capitalize fw-semibold fs-13"
                  disabled={!online}
                  outline={scanType !== type}
                  size="sm"
                >
                  {type === 'quick' && <i className="ri-flashlight-line me-1" />}
                  {type === 'full' && <i className="ri-hard-drive-2-line me-1" />}
                  {type === 'custom' && <i className="ri-folder-search-line me-1" />}
                  {type}
                </Button>
              ))}
            </div>

            {scanType === 'custom' && (
              <div className="mb-3">
                <InputGroup>
                  <span className="input-group-text bg-light border-end-0">
                    <i className="ri-folder-3-line text-muted fs-16" />
                  </span>
                  <Input
                    type="text"
                    value={targetPath}
                    onChange={e => setTargetPath(e.target.value)}
                    placeholder="e.g. C:\Users\Admin\Downloads"
                    disabled={!online}
                    className="border-start-0 ps-0 fs-13"
                  />
                </InputGroup>
              </div>
            )}

            <Button
              color="primary"
              className="w-100 d-flex align-items-center justify-content-center gap-2 fw-bold"
              onClick={handleStartScan}
              disabled={!online || (scanType === 'custom' && !targetPath)}
            >
              <i className="ri-play-circle-line fs-16" />
              Start Scan
            </Button>
          </div>
        )}

        {/* ── Status area ── */}
        <div
          className="flex-grow-1 rounded p-4 d-flex flex-column justify-content-center border"
          style={{
            minHeight: '170px',
            background:
              scanPhase === 'error'
                ? 'rgba(245,158,11,0.05)'
                : scanPhase === 'complete' && scanResult?.threats
                ? 'rgba(239,68,68,0.04)'
                : scanPhase === 'complete'
                ? 'rgba(34,197,94,0.04)'
                : 'var(--bs-light, #f8f9fa)',
            borderColor:
              scanPhase === 'error'
                ? 'rgba(245,158,11,0.3) !important'
                : scanPhase === 'complete' && scanResult?.threats
                ? 'rgba(239,68,68,0.25) !important'
                : scanPhase === 'complete'
                ? 'rgba(34,197,94,0.25) !important'
                : undefined,
          }}
        >
          {/* IDLE */}
          {scanPhase === 'idle' && (
            <div className="text-center">
              <i className="ri-shield-check-line text-muted mb-3 opacity-50 mx-auto d-block fs-32" />
              <h6 className="text-muted fs-13 mb-1">System is protected</h6>
              <p className="text-muted fs-11 mb-0">Select a scan type above to begin.</p>
            </div>
          )}

          {/* SENDING */}
          {scanPhase === 'sending' && (
            <div className="text-center">
              <div
                className="spinner-border mb-3"
                style={{ color: '#6366f1', width: '2rem', height: '2rem' }}
                role="status"
              />
              <h6 className="fs-13 fw-semibold mb-1" style={{ color: '#6366f1' }}>
                Sending command to agent...
              </h6>
              <p className="text-muted fs-11 mb-0">Establishing secure channel</p>
            </div>
          )}

          {/* QUEUED */}
          {scanPhase === 'queued' && (
            <div className="text-center">
              <div className="mb-3 d-flex justify-content-center">
                <span
                  style={{
                    display: 'inline-block',
                    width: '12px',
                    height: '12px',
                    borderRadius: '50%',
                    background: '#f59e0b',
                    boxShadow: '0 0 0 0 rgba(245,158,11,0.5)',
                    animation: 'pulse-ring 1.4s cubic-bezier(0.4,0,0.6,1) infinite',
                  }}
                />
                <style>{`
                  @keyframes pulse-ring {
                    0%, 100% { box-shadow: 0 0 0 0 rgba(245,158,11,0.5); }
                    50% { box-shadow: 0 0 0 8px rgba(245,158,11,0); }
                  }
                `}</style>
              </div>
              <h6 className="fs-13 fw-semibold mb-1" style={{ color: '#f59e0b' }}>
                Command queued
              </h6>
              <p className="text-muted fs-11 mb-0">Waiting for agent to pick up...</p>
            </div>
          )}

          {/* SCANNING */}
          {scanPhase === 'scanning' && (
            <div className="w-100">
              <div className="d-flex justify-content-between align-items-center mb-2">
                <span className="fs-12 fw-semibold" style={{ color: '#6366f1' }}>
                  <i className="ri-loader-4-line me-1" style={{ animation: 'spin 1s linear infinite', display: 'inline-block' }} />
                  Scan in progress
                </span>
                <style>{`@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }`}</style>
              </div>
              <IndeterminateBar />
              <p className="text-muted fs-11 mb-0 mt-3 text-center">
                Scanning files — results will appear when complete
              </p>
              <div className="d-flex justify-content-center mt-3">
                <Button
                  size="sm"
                  color="danger"
                  outline
                  className="d-flex align-items-center gap-1 fs-11"
                  onClick={() => {
                    commandMutation.mutate({ action: 'stop' });
                    handleReset();
                  }}
                >
                  <i className="ri-stop-circle-line fs-13" /> Stop
                </Button>
              </div>
            </div>
          )}

          {/* COMPLETE — clean */}
          {scanPhase === 'complete' && scanResult && scanResult.threats === 0 && (
            <div>
              <div className="text-center mb-3">
                <div
                  className="avatar-md mx-auto mb-3 d-flex align-items-center justify-content-center rounded-circle"
                  style={{ background: 'rgba(34,197,94,0.12)', width: '56px', height: '56px' }}
                >
                  <i className="ri-shield-check-fill fs-28" style={{ color: '#16a34a' }} />
                </div>
                <h6 className="fw-bold fs-14 mb-1" style={{ color: '#16a34a' }}>
                  No threats detected
                </h6>
                <p className="text-muted fs-12 mb-0">System is clean</p>
              </div>
              <div className="d-flex justify-content-center gap-4 py-2 border-top border-bottom mb-3">
                <div className="text-center">
                  <div className="fw-bold fs-16">{scanResult.filesScanned.toLocaleString()}</div>
                  <div className="text-muted fs-10 text-uppercase tracking-wide">Files Scanned</div>
                </div>
                <div className="text-center">
                  <div className="fw-bold fs-16 text-success">0</div>
                  <div className="text-muted fs-10 text-uppercase tracking-wide">Threats</div>
                </div>
              </div>
              {scanResult.scanPath && (
                <p className="text-muted fs-11 font-monospace text-center mb-3 text-truncate">
                  <i className="ri-folder-3-line me-1" />{scanResult.scanPath}
                </p>
              )}
              <Button size="sm" color="light" className="w-100" onClick={handleStartScan}>
                <i className="ri-refresh-line me-1" />Run Again
              </Button>
            </div>
          )}

          {/* COMPLETE — threats found */}
          {scanPhase === 'complete' && scanResult && scanResult.threats > 0 && (
            <div>
              <div className="text-center mb-3">
                <div
                  className="avatar-md mx-auto mb-3 d-flex align-items-center justify-content-center rounded-circle"
                  style={{ background: 'rgba(239,68,68,0.1)', width: '56px', height: '56px' }}
                >
                  <i className="ri-bug-2-fill fs-28 text-danger" />
                </div>
                <h6 className="fw-bold fs-14 mb-1 text-danger">
                  {scanResult.threats} threat{scanResult.threats !== 1 ? 's' : ''} detected
                </h6>
                <p className="text-muted fs-12 mb-0">{scanResult.filesScanned.toLocaleString()} files scanned</p>
              </div>
              {scanResult.threatList.length > 0 && (
                <div
                  className="mb-3 border rounded"
                  style={{ maxHeight: '120px', overflowY: 'auto' }}
                >
                  {scanResult.threatList.map((t, i) => (
                    <div
                      key={i}
                      className="d-flex align-items-start gap-2 px-3 py-2 border-bottom"
                      style={{ fontSize: '11px' }}
                    >
                      <i className="ri-virus-line text-danger mt-1 flex-shrink-0" />
                      <div className="min-width-0">
                        <div className="fw-semibold text-danger text-truncate">{t.threat}</div>
                        <div className="text-muted font-monospace" style={{ wordBreak: 'break-all', fontSize: '10px' }}>{t.file}</div>
                      </div>
                    </div>
                  ))}
                </div>
              )}
              <div className="d-flex gap-2">
                <Button size="sm" color="light" className="flex-grow-1" onClick={handleReset}>
                  <i className="ri-close-line me-1" />Dismiss
                </Button>
                <Button size="sm" color="danger" outline className="flex-grow-1" onClick={handleStartScan}>
                  <i className="ri-refresh-line me-1" />Rescan
                </Button>
              </div>
            </div>
          )}

          {/* ERROR */}
          {scanPhase === 'error' && scanError && (
            <div>
              <div className="text-center mb-3">
                <div
                  className="avatar-md mx-auto mb-3 d-flex align-items-center justify-content-center rounded-circle"
                  style={{ background: 'rgba(245,158,11,0.12)', width: '56px', height: '56px' }}
                >
                  <i className="ri-error-warning-fill fs-28" style={{ color: '#f59e0b' }} />
                </div>
                <h6 className="fw-bold fs-14 mb-1" style={{ color: '#f59e0b' }}>
                  Scan failed
                </h6>
                <p className="text-muted fs-12 mb-2">{scanError.message}</p>
                {scanError.exitCode !== undefined && (
                  <span className="badge bg-light text-muted fs-10 font-monospace">
                    exit code {scanError.exitCode}
                  </span>
                )}
              </div>
              {scanError.details && scanError.details.length > 0 && (
                <div
                  className="mb-3 p-2 rounded bg-light border font-monospace"
                  style={{ fontSize: '10px', maxHeight: '80px', overflowY: 'auto' }}
                >
                  {scanError.details.map((d, i) => <div key={i}>{d}</div>)}
                </div>
              )}
              <div className="d-flex gap-2">
                <Button size="sm" color="light" className="flex-grow-1" onClick={handleReset}>
                  <i className="ri-close-line me-1" />Dismiss
                </Button>
                <Button
                  size="sm"
                  outline
                  className="flex-grow-1"
                  style={{ borderColor: '#f59e0b', color: '#f59e0b' }}
                  onClick={handleStartScan}
                  disabled={!online}
                >
                  <i className="ri-refresh-line me-1" />Retry
                </Button>
              </div>
            </div>
          )}
        </div>

        {/* ── Active scan: show new scan button below ── */}
        {(scanPhase === 'queued' || scanPhase === 'scanning') && (
          <Button
            size="sm"
            color="light"
            className="mt-3 w-100"
            onClick={handleReset}
          >
            <i className="ri-close-line me-1" />Cancel & Reset
          </Button>
        )}
      </CardBody>
    </Card>
  );
};
