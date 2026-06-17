import React, { useState } from 'react';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../../api/endpoints';
import { Card, CardBody, CardHeader, Input, Button, Alert, Collapse } from 'reactstrap';

interface ConfigPushCardProps {
  agentId: string;
  online: boolean;
}

const DEFAULT_CONFIG = {
  "modules": {
    "collector": {
      "enabled": true,
      "interval": 60,
      "sinks": ["manager"]
    },
    "antivirus": {
      "enabled": true,
      "scan_on_execution": true,
      "heuristics_level": "high"
    },
    "fim": {
      "enabled": true,
      "paths": [
        "C:\\Windows\\System32\\drivers\\etc\\hosts",
        "C:\\Program Files\\Risknox"
      ]
    }
  }
};

export const ConfigPushCard: React.FC<ConfigPushCardProps> = ({ agentId, online }) => {
  const [configText, setConfigText] = useState(JSON.stringify(DEFAULT_CONFIG, null, 2));
  const [isJsonValid, setIsJsonValid] = useState(true);
  const [showEditor, setShowEditor] = useState(false);
  const queryClient = useQueryClient();

  const handleConfigChange = (e: React.ChangeEvent<any>) => {
    const val = e.target.value;
    setConfigText(val);
    try {
      JSON.parse(val);
      setIsJsonValid(true);
    } catch (e) {
      setIsJsonValid(false);
    }
  };

  const pushMutation = useMutation({
    mutationFn: (config: any) => endpoints.agents.sendModuleCommand(agentId, 'config_update', { config }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', agentId] });
      setShowEditor(false);
    }
  });

  const handlePush = () => {
    if (!isJsonValid) return;
    try {
      const config = JSON.parse(configText);
      pushMutation.mutate(config);
    } catch(e) {}
  };

  return (
    <Card className="h-100 mb-0">
      <CardHeader className="d-flex align-items-center justify-content-between border-0 pt-4 pb-0 px-4">
        <div className="d-flex align-items-center gap-3">
          <div className="avatar-sm flex-shrink-0">
            <div className="avatar-title bg-secondary-subtle text-secondary rounded fs-18">
              <i className="ri-settings-3-line fs-20"></i>
            </div>
          </div>
          <div>
            <h6 className="fs-15 fw-bold mb-1">Configuration Push</h6>
            <p className="text-muted fs-12 mb-0">Update agent behavior dynamically</p>
          </div>
        </div>
        <Button 
          color="light" 
          size="sm"
          className="d-flex align-items-center gap-2 fw-semibold"
          onClick={() => setShowEditor(!showEditor)}
        >
          <i className="ri-code-s-slash-line fs-16"></i> {showEditor ? 'Hide Editor' : 'Edit JSON'}
        </Button>
      </CardHeader>
      
      <CardBody className="p-4 d-flex flex-column">
        {pushMutation.isSuccess && (
          <Alert color="success" className="d-flex align-items-center gap-2 mb-4 py-2 fs-13">
            <i className="ri-checkbox-circle-line fs-16"></i> Config update command queued successfully
          </Alert>
        )}

        <Collapse isOpen={showEditor} className="flex-grow-1 d-flex flex-column">
          <div className="d-flex align-items-center justify-content-between mb-2">
            <div className="d-flex align-items-center gap-2 text-muted">
              <i className="ri-file-code-line fs-14"></i>
              <span className="fs-11 fw-bold text-uppercase tracking-wide">agent_config.json</span>
            </div>
            {!isJsonValid && (
              <div className="d-flex align-items-center gap-1 text-danger fs-11 fw-bold">
                <i className="ri-alert-line fs-12"></i> Invalid JSON
              </div>
            )}
          </div>
          
          <Input 
            type="textarea"
            value={configText}
            onChange={handleConfigChange}
            className={`flex-grow-1 font-monospace fs-12 p-3 bg-light border ${!isJsonValid ? 'border-danger' : ''}`}
            style={{ minHeight: '220px', resize: 'none' }}
            spellCheck={false}
            disabled={!online || pushMutation.isPending}
          />
          
          <div className="d-flex align-items-center justify-content-end gap-2 mt-3">
            <Button 
              color="light" 
              onClick={() => setConfigText(JSON.stringify(DEFAULT_CONFIG, null, 2))}
              disabled={!online || pushMutation.isPending}
              className="fs-12 fw-semibold"
            >
              Reset to Default
            </Button>
            <Button 
              color="primary"
              onClick={handlePush}
              disabled={!online || !isJsonValid || pushMutation.isPending}
              className="d-flex align-items-center gap-2 fs-12 fw-bold"
            >
              {pushMutation.isPending ? <i className="ri-refresh-line icon-spin fs-16"></i> : <i className="ri-upload-cloud-2-line fs-16"></i>}
              Push to Agent
            </Button>
          </div>
        </Collapse>

        {!showEditor && (
          <div className="flex-grow-1 d-flex flex-column align-items-center justify-content-center text-center p-4 border border-dashed rounded bg-light">
            <i className="ri-settings-3-line fs-32 text-secondary mb-3 opacity-50"></i>
            <h6 className="fs-14 fw-semibold text-body mb-2">Manage Agent Configuration</h6>
            <p className="text-muted fs-12 mb-4 max-w-md mx-auto">
              Modify the agent's internal configuration JSON to dynamically enable or disable modules, change scan intervals, or update detection paths.
            </p>
            <Button 
              color="primary" 
              onClick={() => setShowEditor(true)}
              className="d-flex align-items-center gap-2 fw-semibold shadow-sm"
            >
              <i className="ri-code-s-slash-line fs-16"></i> Open JSON Editor
            </Button>
          </div>
        )}
      </CardBody>
    </Card>
  );
};
