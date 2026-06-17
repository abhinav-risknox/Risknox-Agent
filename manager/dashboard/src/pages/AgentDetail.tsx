import React, { useState } from 'react';
import { useParams, Link } from 'react-router-dom';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints } from '../api/endpoints';
import { Container, Row, Col, Card, CardBody, Button, Spinner, Nav, NavItem, NavLink, TabContent, TabPane, Alert } from 'reactstrap';
import classnames from 'classnames';

import { WebPolicyCard } from '../components/policies/WebPolicyCard';
import { SoftwarePolicyCard } from '../components/policies/SoftwarePolicyCard';
import { AntivirusScanCard } from '../components/policies/AntivirusScanCard';
import { ConfigPushCard } from '../components/policies/ConfigPushCard';
import EndpointTab from '../components/EndpointTab';

import { CommandHistoryTab } from '../components/agent/CommandHistoryTab';
import { StatusReportsTab } from '../components/agent/StatusReportsTab';
import { ModuleControlTab } from '../components/agent/ModuleControlTab';

export const AgentDetail: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const queryClient = useQueryClient();
  const [activeTab, setActiveTab] = useState('control');

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

  const [lastCommand, setLastCommand] = useState<{ verb: string, time: string } | null>(null);

  const commandMutation = useMutation({
    mutationFn: ({ verb, params = {} }: { verb: string, params?: any }) => 
      endpoints.agents.sendModuleCommand(id!, verb, params),
    onSuccess: (_, variables) => {
      queryClient.invalidateQueries({ queryKey: ['agent-history', id] });
      setLastCommand({ verb: variables.verb, time: new Date().toLocaleTimeString() });
      setTimeout(() => setLastCommand(null), 3000);
    }
  });

  const moduleStatusReport = statusReports?.reports?.find((r: any) => r.report_type === 'module_status');
  const moduleStatusData = moduleStatusReport?.report_data || {};
  const lastStatusUpdate = moduleStatusReport?.created_at;

  const toggleTab = (tab: string) => {
    if (activeTab !== tab) {
      setActiveTab(tab);
    }
  };

  if (agentLoading) {
    return (
      <div className="page-content d-flex align-items-center justify-content-center" style={{ minHeight: '60vh' }}>
        <div className="text-center">
          <Spinner color="primary" className="mb-4" style={{ width: '3rem', height: '3rem' }} />
          <h5 className="text-muted text-uppercase tracking-widest fs-13">Initializing Secure Channel...</h5>
        </div>
      </div>
    );
  }

  if (!agent) return <div className="page-content text-center mt-5"><h5>Agent not found</h5></div>;

  return (
    <div className="page-content">
      <Container fluid>
        {/* Header section */}
        <Row className="mb-4 align-items-center">
          <Col md={6}>
            <div className="d-flex align-items-center gap-4">
              <Link to="/agents" className="btn btn-ghost-secondary btn-icon rounded-circle">
                <i className="ri-arrow-left-s-line fs-20"></i>
              </Link>
              <div className="avatar-md flex-shrink-0">
                <div className="avatar-title bg-primary-subtle text-primary rounded-3 fs-24 border border-primary border-opacity-25">
                  <i className="ri-shield-star-line fs-32"></i>
                </div>
              </div>
              <div>
                <div className="d-flex align-items-center gap-3 mb-1">
                  <h4 className="mb-0 fw-bold">{agent.hostname}</h4>
                  <span className={`badge ${agent.online ? 'bg-success-subtle text-success border border-success border-opacity-25' : 'bg-secondary-subtle text-muted'}`}>
                    {agent.online ? 'ONLINE' : 'OFFLINE'}
                  </span>
                </div>
                <p className="text-muted mb-0 fs-12 font-monospace">
                  {agent.agent_id} &bull; {agent.ip_address}
                </p>
              </div>
            </div>
          </Col>
          <Col md={6}>
            <div className="d-flex justify-content-md-end gap-3 mt-3 mt-md-0">
              <div className="text-end bg-light p-2 rounded border">
                <p className="text-muted text-uppercase fw-bold tracking-widest fs-10 mb-1">OS Environment</p>
                <p className="mb-0 fs-13 fw-semibold">{agent.os_type} {agent.os_version}</p>
              </div>
              <div className="text-end bg-light p-2 rounded border">
                <p className="text-muted text-uppercase fw-bold tracking-widest fs-10 mb-1">Agent Version</p>
                <p className="mb-0 fs-13 fw-semibold">{agent.agent_version}</p>
              </div>
              <Button 
                color="primary" 
                className="d-flex align-items-center gap-2 fw-semibold"
                onClick={() => commandMutation.mutate({ verb: 'status_request' })}
                disabled={!agent.online || commandMutation.isPending}
              >
                {commandMutation.isPending ? <i className="ri-refresh-line fs-16 icon-spin"></i> : <i className="ri-refresh-line fs-16"></i>}
                Refresh Status
              </Button>
            </div>
          </Col>
        </Row>

        {lastCommand && (
          <Alert color="warning" className="d-flex align-items-center gap-3 border-0 shadow-sm animate-in fade-in">
            <div className="avatar-xs flex-shrink-0">
              <div className="avatar-title bg-warning-subtle text-warning rounded-circle fs-16">
                <i className="ri-checkbox-circle-line fs-16"></i>
              </div>
            </div>
            <div className="flex-grow-1">
              <span className="fw-bold text-uppercase tracking-widest fs-12 text-warning me-3">Command Dispatched</span>
              <span className="text-muted fs-13 font-monospace">verb={lastCommand.verb} @ {lastCommand.time}</span>
            </div>
          </Alert>
        )}

        <Card>
          <CardBody className="p-0">
            <Nav tabs className="nav-tabs-custom nav-primary px-4 pt-3 border-bottom-0">
              <NavItem>
                <NavLink
                  className={classnames({ active: activeTab === 'control' }, 'd-flex align-items-center gap-2 fw-semibold py-3 cursor-pointer')}
                  onClick={() => toggleTab('control')}
                >
                  <i className="ri-flashlight-fill fs-16"></i> Module Control
                </NavLink>
              </NavItem>
              <NavItem>
                <NavLink
                  className={classnames({ active: activeTab === 'policies' }, 'd-flex align-items-center gap-2 fw-semibold py-3 cursor-pointer')}
                  onClick={() => toggleTab('policies')}
                >
                  <i className="ri-shield-check-line fs-16"></i> Security Policies
                </NavLink>
              </NavItem>
              <NavItem>
                <NavLink
                  className={classnames({ active: activeTab === 'endpoint' }, 'd-flex align-items-center gap-2 fw-semibold py-3 cursor-pointer')}
                  onClick={() => toggleTab('endpoint')}
                >
                  <i className="ri-terminal-box-line fs-16"></i> Endpoint Management
                </NavLink>
              </NavItem>
              <NavItem>
                <NavLink
                  className={classnames({ active: activeTab === 'history' }, 'd-flex align-items-center gap-2 fw-semibold py-3 cursor-pointer')}
                  onClick={() => toggleTab('history')}
                >
                  <i className="ri-time-line fs-16"></i> Command History
                </NavLink>
              </NavItem>
              <NavItem>
                <NavLink
                  className={classnames({ active: activeTab === 'status' }, 'd-flex align-items-center gap-2 fw-semibold py-3 cursor-pointer')}
                  onClick={() => toggleTab('status')}
                >
                  <i className="ri-pulse-line fs-16"></i> Status Reports
                </NavLink>
              </NavItem>
            </Nav>
          </CardBody>
        </Card>

        <div className="mt-4">
          <TabContent activeTab={activeTab}>
            <TabPane tabId="control">
              <ModuleControlTab agent={agent} commandMutation={commandMutation} />
            </TabPane>

            <TabPane tabId="policies">
              <div className="d-flex align-items-center justify-content-between mb-3">
                <div>
                  <h5 className="fs-14 fw-bold text-muted text-uppercase tracking-wide mb-1">Active Policy State</h5>
                  {lastStatusUpdate && <p className="text-muted fs-11 font-monospace mb-0">Snapshot from: {lastStatusUpdate}</p>}
                </div>
                <Button 
                  color="light" 
                  size="sm"
                  className="d-flex align-items-center gap-2"
                  onClick={() => commandMutation.mutate({ verb: 'status_request' })}
                  disabled={!agent.online || commandMutation.isPending}
                >
                  <i className={`ri-refresh-line fs-14 ${commandMutation.isPending ? 'icon-spin' : ''}`}></i> Refresh from Agent
                </Button>
              </div>

              <Row className="g-4">
                <Col lg={6}>
                  <WebPolicyCard 
                    agentId={id!} 
                    blockedUrls={moduleStatusData.web_blocking?.blockedUrls || []} 
                    online={agent.online} 
                  />
                </Col>
                <Col lg={6}>
                  <SoftwarePolicyCard 
                    agentId={id!} 
                    blockedApps={moduleStatusData.software_blocking?.blockedApps || []} 
                    online={agent.online} 
                  />
                </Col>
                <Col lg={6}>
                  <AntivirusScanCard 
                    agentId={id!} 
                    online={agent.online} 
                  />
                </Col>
                <Col lg={6}>
                  <ConfigPushCard 
                    agentId={id!} 
                    online={agent.online} 
                  />
                </Col>
              </Row>
            </TabPane>

            <TabPane tabId="endpoint">
              <EndpointTab agentId={id!} />
            </TabPane>

            <TabPane tabId="history">
              <CommandHistoryTab history={history} />
            </TabPane>

            <TabPane tabId="status">
              <StatusReportsTab statusReports={statusReports} />
            </TabPane>
          </TabContent>
        </div>
      </Container>
    </div>
  );
};
