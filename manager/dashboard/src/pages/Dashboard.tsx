import React from 'react';
import { Container, Row, Col, Card, CardBody, CardHeader, Table, Button, Spinner } from 'reactstrap';
import { Link } from 'react-router-dom';
import { StatCard } from '../components/dashboard/StatCard';
import { useAgents } from '../hooks/useAgents';

export const Dashboard: React.FC = () => {
  const { data, isLoading, isError, refetch, isFetching } = useAgents();

  const totalAgents = data?.count || 0;
  const onlineAgents = data?.agents.filter(a => a.online).length || 0;
  
  const threatsBlocked = 42; 
  const commandsSent = 2401;

  document.title = "Dashboard | RiskNoX Manager";

  return (
    <div className="page-content">
      <Container fluid>
        {/* Page Title */}
        <Row>
          <Col xs={12}>
            <div className="page-title-box d-sm-flex align-items-center justify-content-between">
              <h4 className="mb-sm-0">Overview</h4>
              <div className="page-title-right">
                <Button 
                  color="light" 
                  className="btn-icon btn-sm me-2" 
                  onClick={() => refetch()} 
                  disabled={isFetching}
                >
                  {isFetching ? <Spinner size="sm" /> : <i className="ri-refresh-line"></i>}
                </Button>
                <Button color="primary" className="btn-sm">
                  <i className="ri-add-line align-middle me-1"></i> Deploy Agent
                </Button>
              </div>
            </div>
          </Col>
        </Row>

        {/* Stats Grid */}
        <Row>
          <Col xl={3} md={6}>
            <StatCard 
              title="Total Agents" 
              value={isLoading ? "..." : totalAgents} 
              iconClass="ri-team-line" 
              trend="12%" 
              trendUp={true} 
              color="primary"
            />
          </Col>
          <Col xl={3} md={6}>
            <StatCard 
              title="Active Sessions" 
              value={isLoading ? "..." : onlineAgents} 
              iconClass="ri-pulse-line" 
              trend="5%" 
              trendUp={true} 
              color="success"
            />
          </Col>
          <Col xl={3} md={6}>
            <StatCard 
              title="Threats Blocked" 
              value={threatsBlocked} 
              iconClass="ri-shield-check-line" 
              trend="24%" 
              trendUp={false} 
              color="warning"
            />
          </Col>
          <Col xl={3} md={6}>
            <StatCard 
              title="Commands Sent" 
              value={commandsSent} 
              iconClass="ri-flashlight-fill" 
              trend="8%" 
              trendUp={true} 
              color="info"
            />
          </Col>
        </Row>

        <Row>
          {/* Main Agent Table Preview */}
          <Col xl={8}>
            <Card>
              <CardHeader className="align-items-center d-flex">
                <h4 className="card-title mb-0 flex-grow-1">Connected Agents</h4>
                <div className="flex-shrink-0">
                  <Link to="/agents" className="text-primary fw-semibold">
                    View All <i className="ri-arrow-right-up-line"></i>
                  </Link>
                </div>
              </CardHeader>
              <CardBody>
                <div className="table-responsive table-card">
                  {isLoading ? (
                    <div className="text-center p-5">
                      <Spinner color="primary" />
                      <p className="mt-2 text-muted">Loading Agents...</p>
                    </div>
                  ) : isError ? (
                    <div className="text-center p-5 text-danger">
                      <i className="ri-error-warning-line display-5"></i>
                      <p className="mt-2">Connection Error. Could not reach the Manager API.</p>
                    </div>
                  ) : (
                    <Table className="align-middle table-nowrap mb-0">
                      <thead className="table-light">
                        <tr>
                          <th scope="col">Agent ID</th>
                          <th scope="col">Hostname</th>
                          <th scope="col">System</th>
                          <th scope="col">Status</th>
                          <th scope="col">Action</th>
                        </tr>
                      </thead>
                      <tbody>
                        {data?.agents.slice(0, 5).map((agent) => (
                          <tr key={agent.agent_id}>
                            <td>
                              <span className="text-primary fw-medium">{agent.agent_id.substring(0, 8)}...</span>
                            </td>
                            <td>
                              <div className="d-flex align-items-center">
                                <div className="flex-shrink-0 me-2">
                                  <div className="avatar-xs">
                                    <span className="avatar-title bg-light text-secondary rounded">
                                      <i className="ri-computer-line"></i>
                                    </span>
                                  </div>
                                </div>
                                <div className="flex-grow-1">
                                  <h5 className="fs-14 mb-0">{agent.hostname}</h5>
                                </div>
                              </div>
                            </td>
                            <td>
                              {agent.os_type} <span className="text-muted fs-11">({agent.os_version})</span>
                            </td>
                            <td>
                              {agent.online ? (
                                <span className="badge bg-success-subtle text-success">
                                  <i className="ri-checkbox-blank-circle-fill fs-10 align-middle me-1 pulse-success"></i> Online
                                </span>
                              ) : (
                                <span className="badge bg-danger-subtle text-danger">
                                  <i className="ri-checkbox-blank-circle-fill fs-10 align-middle me-1"></i> Offline
                                </span>
                              )}
                            </td>
                            <td>
                              <Link to={`/agents/${agent.agent_id}`} className="btn btn-sm btn-soft-secondary">
                                Details
                              </Link>
                            </td>
                          </tr>
                        ))}
                        {data?.agents.length === 0 && (
                          <tr>
                            <td colSpan={5} className="text-center p-4 text-muted">
                              No agents registered
                            </td>
                          </tr>
                        )}
                      </tbody>
                    </Table>
                  )}
                </div>
              </CardBody>
            </Card>
          </Col>

          {/* Recent Activity Sidebar */}
          <Col xl={4}>
            <Card>
              <CardHeader className="align-items-center d-flex">
                <h4 className="card-title mb-0 flex-grow-1">Recent Activity</h4>
              </CardHeader>
              <CardBody>
                <div className="profile-timeline">
                  <div className="accordion accordion-flush" id="accordionFlushExample">
                    {[1, 2, 3, 4].map((i) => (
                      <div className="accordion-item border-0" key={i}>
                        <div className="accordion-header">
                          <a className="accordion-button p-2 shadow-none" href="#">
                            <div className="d-flex align-items-center">
                              <div className="flex-shrink-0 avatar-xs">
                                <div className="avatar-title bg-primary-subtle text-primary rounded-circle">
                                  <i className="ri-terminal-box-line"></i>
                                </div>
                              </div>
                              <div className="flex-grow-1 ms-3">
                                <h6 className="fs-14 mb-0">Module Control</h6>
                                <small className="text-muted">Command status_request sent to agent cluster</small>
                              </div>
                            </div>
                          </a>
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
                <div className="mt-3 text-center">
                  <Link to="/audit" className="text-muted text-decoration-underline">View all Activity</Link>
                </div>
              </CardBody>
            </Card>
          </Col>
        </Row>
      </Container>
    </div>
  );
};
