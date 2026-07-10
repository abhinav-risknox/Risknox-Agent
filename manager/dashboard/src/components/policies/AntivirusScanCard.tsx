import React, { useState } from 'react';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { Card, CardBody, CardHeader, Input, Button, InputGroup, Progress } from 'reactstrap';

interface AntivirusScanCardProps {
  agentId: string;
  online: boolean;
}

export const AntivirusScanCard: React.FC<AntivirusScanCardProps> = ({ agentId, online }) => {
  const [targetPath, setTargetPath] = useState('C:\\');
  const [scanType, setScanType] = useState<'quick' | 'full' | 'custom'>('quick');
  const queryClient = useQueryClient();

  const commandMutation = useMutation({
    mutationFn: (data: any) => endpoints.agents.sendPolicy(agentId, 'antivirus', data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      queryClient.invalidateQueries({ queryKey: ['agent-status', agentId] });
    }
  });

  const { data: statusReports } = useQuery({
    queryKey: ['agent-status', agentId],
    queryFn: () => endpoints.agents.status(agentId).then(res => res.data),
    refetchInterval: 5000,
  });

  const avStatusReport = statusReports?.reports?.find((r: any) => r.report_type === 'module_status');
  const avState = avStatusReport?.report_data?.antivirus || { state: 'idle', definitions: 'unknown' };

  const handleStartScan = () => {
    commandMutation.mutate({ 
      action: 'start', 
      type: scanType, 
      path: scanType === 'custom' ? targetPath : undefined 
    });
  };

  const handleStopScan = () => {
    commandMutation.mutate({ action: 'stop' });
  };

  const isScanning = avState.state === 'scanning';
  const progress = avState.progress || 0;

  return (
    <Card className="h-100 mb-0">
      <CardHeader className="d-flex align-items-center border-0 pt-4 pb-0 px-4">
        <div className="d-flex align-items-center gap-3">
          <div className="avatar-sm flex-shrink-0">
            <div className="avatar-title bg-success-subtle text-success rounded fs-18">
              <i className="ri-shield-check-line fs-20"></i>
            </div>
          </div>
          <div>
            <h6 className="fs-15 fw-bold mb-1">Antivirus Scanner</h6>
            <div className="d-flex align-items-center gap-2">
              <span className={`badge ${isScanning ? 'bg-warning' : 'bg-success-subtle text-success'}`}>
                {isScanning ? 'Scanning...' : 'Idle'}
              </span>
              <p className="text-muted fs-11 font-monospace mb-0 tracking-tight">Defs: {avState.definitions}</p>
            </div>
          </div>
        </div>
      </CardHeader>
      
      <CardBody className="p-4 d-flex flex-column">
        <div className="mb-4">
          <div className="d-flex gap-2 mb-3">
            {(['quick', 'full', 'custom'] as const).map(type => (
              <Button
                key={type}
                color={scanType === type ? 'success' : 'light'}
                onClick={() => setScanType(type)}
                className="flex-grow-1 text-capitalize fw-semibold fs-13"
                disabled={!online || isScanning}
                outline={scanType !== type}
              >
                {type}
              </Button>
            ))}
          </div>

          {scanType === 'custom' && (
            <div className="mb-3">
              <InputGroup>
                <span className="input-group-text bg-light border-end-0">
                  <i className="ri-hard-drive-2-line text-muted fs-16"></i>
                </span>
                <Input 
                  type="text" 
                  value={targetPath}
                  onChange={(e) => setTargetPath(e.target.value)}
                  placeholder="Target path (e.g. C:\Users\Admin\Downloads)"
                  disabled={!online || isScanning}
                  className="border-start-0 ps-0"
                />
              </InputGroup>
            </div>
          )}

          <div className="d-flex gap-2">
            <Button 
              color="success" 
              className="flex-grow-1 d-flex align-items-center justify-content-center gap-2 fw-bold"
              onClick={handleStartScan}
              disabled={!online || isScanning || commandMutation.isPending || (scanType === 'custom' && !targetPath)}
            >
              {commandMutation.isPending ? <i className="ri-refresh-line icon-spin fs-16"></i> : <i className="ri-play-line fs-16"></i>}
              Start Scan
            </Button>
            <Button 
              color="danger" 
              className="d-flex align-items-center justify-content-center gap-2 fw-bold"
              onClick={handleStopScan}
              disabled={!online || !isScanning || commandMutation.isPending}
            >
              <i className="ri-stop-circle-line fs-16"></i> Stop
            </Button>
          </div>
        </div>

        <div className="flex-grow-1 bg-light rounded p-4 d-flex flex-column justify-content-center border" style={{ minHeight: '160px' }}>
          {isScanning ? (
            <div className="w-100">
              <div className="d-flex justify-content-between mb-2">
                <span className="fs-12 fw-semibold text-primary">Scan in progress...</span>
                <span className="fs-12 fw-bold text-primary">{progress}%</span>
              </div>
              <Progress value={progress} color="primary" className="mb-3" style={{ height: '8px' }} />
              
              <div className="d-flex align-items-center gap-2 text-muted fs-11 font-monospace">
                <i className="ri-file-search-line fs-14 flex-shrink-0"></i>
                <span className="text-truncate">{avState.current_file || 'Scanning system files...'}</span>
              </div>
            </div>
          ) : (
            <div className="text-center">
              <i className="ri-shield-check-line text-muted mb-3 opacity-50 mx-auto d-block fs-32"></i>
              <h6 className="text-muted fs-13 mb-1">System is protected</h6>
              <p className="text-muted fs-11 mb-0">Select a scan type above to begin manual analysis.</p>
            </div>
          )}
        </div>

        {avState.last_scan && (
          <div className="mt-3 p-3 border rounded bg-white d-flex align-items-center justify-content-between">
            <div>
              <p className="fs-11 fw-bold text-uppercase text-muted mb-1 tracking-wide">Last Scan Results</p>
              <div className="d-flex align-items-center gap-3">
                <span className="fs-12 font-monospace">{avState.last_scan.time}</span>
                <span className={`badge ${avState.last_scan.threats_found > 0 ? 'bg-danger' : 'bg-success-subtle text-success'}`}>
                  {avState.last_scan.threats_found} threats found
                </span>
              </div>
            </div>
            {avState.last_scan.threats_found > 0 && (
              <div className="avatar-xs flex-shrink-0">
                <div className="avatar-title bg-danger-subtle text-danger rounded">
                  <i className="ri-bug-line fs-14"></i>
                </div>
              </div>
            )}
          </div>
        )}
      </CardBody>
    </Card>
  );
};
