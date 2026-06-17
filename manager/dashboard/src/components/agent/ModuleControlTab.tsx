import React from 'react';
import { Row, Col, Card, CardBody } from 'reactstrap';

interface ModuleControlTabProps {
  agent: any;
  commandMutation: any;
}

export const ModuleControlTab: React.FC<ModuleControlTabProps> = ({ agent, commandMutation }) => {
  const controlButtons = [
    { label: 'Collector Start', verb: 'collector_start', icon: 'ri-play-line', color: 'success' },
    { label: 'Collector Stop', verb: 'collector_stop', icon: 'ri-stop-line', color: 'warning' },
    { label: 'FIM Start', verb: 'fim_start', icon: 'ri-shield-check-line', color: 'success' },
    { label: 'FIM Stop', verb: 'fim_stop', icon: 'ri-shield-keyhole-line', color: 'warning' },
    { label: 'Worker Restart', verb: 'worker_restart', icon: 'ri-refresh-line', color: 'primary' },
    { label: 'AV Version', verb: 'av_version', icon: 'ri-pulse-line', color: 'primary' },
    { label: 'Status Request', verb: 'status_request', icon: 'ri-pulse-line', color: 'primary' },
    { label: 'Diagnostics', verb: 'diagnostics', icon: 'ri-terminal-box-line', color: 'info', params: { detail: 'full' } },
    { label: 'Agent Restart', verb: 'agent_restart', icon: 'ri-power-line', color: 'danger', danger: true },
  ];

  return (
    <Row className="g-4">
      {controlButtons.map((btn) => (
        <Col md={6} lg={4} key={btn.verb}>
          <Card 
            className={`h-100 mb-0 cursor-pointer border ${!agent.online ? 'opacity-50' : 'hover-shadow'}`}
            onClick={() => {
              if (agent.online && !commandMutation.isPending) {
                commandMutation.mutate({ verb: btn.verb, params: btn.params });
              }
            }}
            style={{ 
              cursor: (!agent.online || commandMutation.isPending) ? 'not-allowed' : 'pointer',
              transition: 'all 0.2s ease-in-out'
            }}
          >
            <CardBody className="p-4 d-flex flex-column align-items-start">
              <div className={`avatar-md mb-4`}>
                <div className={`avatar-title bg-${btn.color}-subtle text-${btn.color} rounded-3 fs-24`}>
                  <i className={btn.icon}></i>
                </div>
              </div>
              <h5 className="fs-16 fw-semibold mb-1">{btn.label}</h5>
              <p className="text-muted fs-12 font-monospace text-uppercase tracking-wide mb-0">{btn.verb}</p>
              
              {!agent.online && (
                <p className="text-warning fs-11 fw-bold mt-auto pt-3 mb-0">Offline: Command Buffer Only</p>
              )}
            </CardBody>
          </Card>
        </Col>
      ))}
    </Row>
  );
};
