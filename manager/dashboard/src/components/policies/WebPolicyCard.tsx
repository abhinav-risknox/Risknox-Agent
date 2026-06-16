import React, { useState } from 'react';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { Card, CardBody, CardHeader, Input, Button, InputGroup, Form } from 'reactstrap';

interface WebPolicyCardProps {
  agentId: string;
  blockedUrls: string[];
  online: boolean;
}

export const WebPolicyCard: React.FC<WebPolicyCardProps> = ({ agentId, blockedUrls, online }) => {
  const [url, setUrl] = useState('');
  const queryClient = useQueryClient();

  const policyMutation = useMutation({
    mutationFn: (data: any) => endpoints.agents.sendPolicy(agentId, 'web_blocking', data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      queryClient.invalidateQueries({ queryKey: ['agent-status', agentId] });
      setUrl('');
    }
  });

  const handleAdd = (e: React.FormEvent) => {
    e.preventDefault();
    if (!url) return;
    policyMutation.mutate({ action: 'block', url });
  };

  const handleRemove = (urlToRemove: string) => {
    policyMutation.mutate({ action: 'unblock', url: urlToRemove });
  };

  return (
    <Card className="h-100 mb-0">
      <CardHeader className="d-flex align-items-center border-0 pt-4 pb-0 px-4">
        <div className="d-flex align-items-center gap-3">
          <div className="avatar-sm flex-shrink-0">
            <div className="avatar-title bg-info-subtle text-info rounded fs-18">
              <i className="ri-global-line fs-20"></i>
            </div>
          </div>
          <div>
            <h6 className="fs-15 fw-bold mb-1">Web Traffic Blocking</h6>
            <p className="text-muted fs-12 mb-0">DNS and Proxy level URL filtering</p>
          </div>
        </div>
      </CardHeader>
      
      <CardBody className="p-4 d-flex flex-column">
        <Form onSubmit={handleAdd} className="mb-4">
          <InputGroup>
            <Input 
              type="text" 
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              placeholder="Enter domain (e.g. facebook.com)"
              disabled={!online || policyMutation.isPending}
              required
            />
            <Button 
              color="info" 
              type="submit"
              disabled={!online || policyMutation.isPending || !url}
              className="d-flex align-items-center justify-content-center text-white"
              style={{ width: '46px' }}
            >
              {policyMutation.isPending ? <i className="ri-refresh-line icon-spin fs-16"></i> : <i className="ri-add-line fs-16"></i>}
            </Button>
          </InputGroup>
        </Form>

        <div className="flex-grow-1 overflow-auto pe-2" style={{ minHeight: '200px' }}>
          {(!blockedUrls || blockedUrls.length === 0) ? (
            <div className="h-100 d-flex flex-column items-center justify-content-center text-muted fst-italic fs-13 text-center">
              No URLs currently blocked
            </div>
          ) : (
            <div className="d-flex flex-column gap-2">
              {blockedUrls.map((item: any, idx: number) => {
                const itemUrl = typeof item === 'string' ? item : item.url;
                const status = typeof item === 'object' ? item.status : null;
                const source = typeof item === 'object' ? item.source : null;
                
                return (
                  <div key={idx} className="d-flex align-items-center justify-content-between p-3 border rounded bg-light hover-shadow transition-all">
                    <div className="d-flex align-items-center gap-3">
                      <div className="avatar-xs flex-shrink-0">
                        <div className="avatar-title bg-white border text-secondary rounded">
                          <i className="ri-link fs-14"></i>
                        </div>
                      </div>
                      <div>
                        <p className="fs-13 fw-semibold text-body mb-0 font-monospace">{itemUrl}</p>
                        {(status || source) && (
                          <div className="d-flex align-items-center gap-2 mt-1">
                            {status && <span className={`badge ${status === 'active' ? 'bg-danger-subtle text-danger' : 'bg-warning-subtle text-warning'} fs-10 tracking-widest text-uppercase`}>{status}</span>}
                            {source && <span className="text-muted fs-11 font-monospace">Source: {source}</span>}
                          </div>
                        )}
                      </div>
                    </div>
                    <Button 
                      color="ghost-danger" 
                      size="sm"
                      className="btn-icon"
                      onClick={() => handleRemove(itemUrl)}
                      disabled={!online || policyMutation.isPending}
                    >
                      <i className="ri-delete-bin-line fs-16"></i>
                    </Button>
                  </div>
                );
              })}
            </div>
          )}
        </div>
      </CardBody>
    </Card>
  );
};
