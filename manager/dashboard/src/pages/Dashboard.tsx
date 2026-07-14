import React from 'react';
import { Container, Row, Col, Card, CardBody, CardHeader, Table, Button, Spinner } from 'reactstrap';
import { Link } from 'react-router-dom';
import { useQuery } from '@tanstack/react-query';
import { endpoints } from '../api/endpoints';
import { StatCard } from '../components/dashboard/StatCard';
import { WorldMap } from '../components/dashboard/WorldMap';
import { useAgents } from '../hooks/useAgents';

const countryFlag = (cc: string) => {
  if (!cc) return '';
  return cc.toUpperCase().split('').map(c => String.fromCodePoint(0x1F1E6 - 65 + c.charCodeAt(0))).join('');
};

export const Dashboard: React.FC = () => {
  const { data, isLoading, isError, refetch, isFetching } = useAgents();
  const { data: geoStats, isLoading: geoLoading } = useQuery({
    queryKey: ['geo-stats'],
    queryFn: () => endpoints.geo.stats().then(r => r.data),
    refetchInterval: 60_000,
  });

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
              value={isLoading ? "..." : `${totalAgents} / ${data?.max_agents || 100}`} 
              iconClass="ri-team-line" 
              trend={data?.limit_reached ? "Limit Reached" : "12%"} 
              trendUp={!data?.limit_reached} 
              color={data?.limit_reached ? "danger" : "primary"}
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

          {/* Geo Distribution Sidebar */}
          <Col xl={4}>
            <Card>
              <CardHeader className="align-items-center d-flex">
                <h4 className="card-title mb-0 flex-grow-1">Top Countries</h4>
              </CardHeader>
              <CardBody>
                {geoLoading ? (
                  <div className="text-center py-4">
                    <Spinner size="sm" color="primary" />
                  </div>
                ) : !geoStats || geoStats.by_country.length === 0 ? (
                  <div className="text-center py-4 text-muted">
                    No geo data available yet.
                  </div>
                ) : (
                  <div>
                    <div className="d-flex justify-content-between text-muted fs-11 fw-semibold text-uppercase tracking-widest mb-2 px-2">
                      <span>Country</span>
                      <span>Agents</span>
                    </div>
                    <div className="d-flex flex-column gap-2">
                      {geoStats.by_country.slice(0, 8).map((c, i) => (
                        <div key={i} className="d-flex align-items-center justify-content-between bg-light p-2 rounded transition-all hover-shadow">
                          <div className="d-flex align-items-center gap-2">
                            <span className="fs-16">{countryFlag(c.country_code)}</span>
                            <span className="fs-13 fw-medium">{c.country}</span>
                          </div>
                          <span className="fs-13 fw-bold">{c.count}</span>
                        </div>
                      ))}
                    </div>
                  </div>
                )}
                
                {geoStats && (geoStats.proxies_detected > 0 || geoStats.hosting_detected > 0) && (
                  <div className="mt-4 pt-3 border-top">
                    <h6 className="fs-12 text-muted fw-semibold text-uppercase tracking-widest mb-3">Network Insights</h6>
                    <Row className="g-2">
                      <Col xs={6}>
                        <div className="bg-warning-subtle text-warning p-2 rounded text-center">
                          <h4 className="mb-0 fs-18 fw-bold">{geoStats.proxies_detected}</h4>
                          <span className="fs-10 tracking-widest">PROXIES</span>
                        </div>
                      </Col>
                      <Col xs={6}>
                        <div className="bg-info-subtle text-info p-2 rounded text-center">
                          <h4 className="mb-0 fs-18 fw-bold">{geoStats.hosting_detected}</h4>
                          <span className="fs-10 tracking-widest">HOSTING</span>
                        </div>
                      </Col>
                    </Row>
                  </div>
                )}
              </CardBody>
            </Card>
          </Col>
        </Row>

        <Row>
          <Col xs={12}>
            <Card>
              <CardHeader className="align-items-center d-flex">
                <h4 className="card-title mb-0 flex-grow-1">Global Agent Distribution</h4>
                <div className="flex-shrink-0">
                  <span className="badge bg-primary-subtle text-primary">
                    <i className="ri-map-pin-2-fill fs-10 align-middle me-1 pulse-success"></i>
                    {geoStats?.total_agents_with_geo || 0} mapped
                  </span>
                </div>
              </CardHeader>
              <CardBody>
                {geoLoading ? (
                  <div className="text-center py-4">
                    <Spinner size="sm" color="primary" />
                  </div>
                ) : !geoStats || geoStats.by_country.length === 0 ? (
                  <div className="text-center py-4 text-muted">
                    No geo data available yet.
                  </div>
                ) : (
                  <WorldMap data={geoStats.by_country} agents={data?.agents || []} />
                )}
              </CardBody>
            </Card>
          </Col>
        </Row>
      </Container>
    </div>
  );
};
