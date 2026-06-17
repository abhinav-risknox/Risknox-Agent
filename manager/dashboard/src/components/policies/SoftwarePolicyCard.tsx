import React, { useState } from 'react';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { Card, CardBody, CardHeader, Input, Button, InputGroup, Form } from 'reactstrap';

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
    <Card className="h-100 mb-0">
      <CardHeader className="d-flex align-items-center border-0 pt-4 pb-0 px-4">
        <div className="d-flex align-items-center gap-3">
          <div className="avatar-sm flex-shrink-0">
            <div className="avatar-title bg-primary-subtle text-primary rounded fs-18">
              <i className="ri-layout-grid-line fs-20"></i>
            </div>
          </div>
          <div>
            <h6 className="fs-15 fw-bold mb-1">Software Blocking</h6>
            <p className="text-muted fs-12 mb-0">Process termination and registry restriction</p>
          </div>
        </div>
      </CardHeader>
      
      <CardBody className="p-4 d-flex flex-column">
        <Form onSubmit={handleAdd} className="mb-4">
          <Input 
            type="text" 
            value={appName}
            onChange={(e) => setAppName(e.target.value)}
            placeholder="App Display Name (Optional)"
            className="mb-2"
            disabled={!online || policyMutation.isPending}
          />
          <InputGroup>
            <Input 
              type="text" 
              value={appExe}
              onChange={(e) => setAppExe(e.target.value)}
              placeholder="Executable name (e.g. chrome.exe)"
              disabled={!online || policyMutation.isPending}
              required
            />
            <Button 
              color="primary" 
              type="submit"
              disabled={!online || policyMutation.isPending || !appExe}
              className="d-flex align-items-center justify-content-center"
              style={{ width: '46px' }}
            >
              {policyMutation.isPending ? <i className="ri-refresh-line icon-spin fs-16"></i> : <i className="ri-add-line fs-16"></i>}
            </Button>
          </InputGroup>
        </Form>

        <div className="flex-grow-1 overflow-auto pe-2" style={{ minHeight: '200px' }}>
          {(!blockedApps || blockedApps.length === 0) ? (
            <div className="h-100 d-flex flex-column items-center justify-content-center text-muted fst-italic fs-13 text-center">
              No applications blocked
            </div>
          ) : (
            <div className="d-flex flex-column gap-2">
              {blockedApps.map((item, idx) => (
                <div key={idx} className="d-flex align-items-center justify-content-between p-3 border rounded bg-light hover-shadow transition-all">
                  <div className="d-flex align-items-center gap-3">
                    <div className="avatar-xs flex-shrink-0">
                      <div className="avatar-title bg-white border text-secondary rounded">
                        <i className="ri-cpu-line fs-14"></i>
                      </div>
                    </div>
                    <div>
                      <p className="fs-13 fw-bold text-body mb-1">{item.name || item.executable}</p>
                      <div className="d-flex align-items-center gap-2">
                        <p className="fs-11 text-muted font-monospace mb-0">{item.executable}</p>
                        <span className="badge bg-warning-subtle text-warning text-uppercase tracking-tighter fs-10">Kills: {item.kills}</span>
                      </div>
                    </div>
                  </div>
                  <Button 
                    color="ghost-danger" 
                    size="sm"
                    className="btn-icon"
                    onClick={() => handleRemove(item.executable)}
                    disabled={!online || policyMutation.isPending}
                  >
                    <i className="ri-delete-bin-line fs-16"></i>
                  </Button>
                </div>
              ))}
            </div>
          )}
        </div>
      </CardBody>
    </Card>
  );
};
