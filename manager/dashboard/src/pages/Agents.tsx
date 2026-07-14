import React from 'react';
import { useAgents } from '../hooks/useAgents';
import { Link } from 'react-router-dom';
import { Container, Row, Col, Card, CardHeader, CardBody, Table, Input, Button, Spinner, InputGroup, InputGroupText, UncontrolledDropdown, DropdownToggle, DropdownMenu, DropdownItem } from 'reactstrap';
import { endpoints } from '../api/endpoints';

const countryFlag = (cc: string) => {
  if (!cc) return '';
  return cc.toUpperCase().split('').map(c => String.fromCodePoint(0x1F1E6 - 65 + c.charCodeAt(0))).join('');
};

export const Agents: React.FC = () => {
  const { data, isLoading, isFetching, refetch, isError } = useAgents();

  document.title = "Agent Fleet | RiskNoX Manager";

  const handleDeleteAgent = async (id: string) => {
    if (confirm(`Are you sure you want to completely remove agent ${id}? This action cannot be undone.`)) {
      try {
        await endpoints.agents.delete(id);
        refetch();
      } catch (err: any) {
        alert(err.response?.data?.error || 'Failed to delete agent');
      }
    }
  };

  return (
    <div className="page-content">
      <Container fluid>
        {/* Page Title */}
        <Row>
          <Col xs={12}>
            <div className="page-title-box d-sm-flex align-items-center justify-content-between">
              <div>
                <h4 className="mb-sm-0">Agent Fleet</h4>
                <p className="text-muted mt-1 mb-0">Manage and monitor your endpoints across all environments.</p>
              </div>
              <div className="page-title-right">
                  <Button 
                    color="light" 
                    className="btn-icon btn-sm" 
                    onClick={() => refetch()} 
                    disabled={isFetching}
                  >
                    {isFetching ? <Spinner size="sm" /> : <i className="ri-refresh-line fs-14"></i>}
                  </Button>
              </div>
            </div>
          </Col>
        </Row>

        {/* Main Content */}
        <Row>
          <Col lg={12}>
            <Card>
              <CardHeader className="d-flex align-items-center border-0 pb-0">
                <div className="flex-grow-1">
                  <InputGroup style={{ maxWidth: '300px' }}>
                    <InputGroupText>
                      <i className="ri-search-line fs-14"></i>
                    </InputGroupText>
                    <Input 
                      type="text" 
                      placeholder="Filter agents by ID, hostname, or IP..."
                    />
                  </InputGroup>
                </div>
                <div className="flex-shrink-0 d-flex gap-2 align-items-center">
                  <Button color="light" size="sm" className="d-flex align-items-center gap-1">
                    <i className="ri-filter-3-line fs-14"></i> Filter
                  </Button>
                  <div className="vr d-none d-sm-inline-block mx-2"></div>
                  <span className="text-muted text-uppercase fw-semibold fs-12">
                    Total: <span className="text-body fw-bold">{data?.count || 0}</span>
                  </span>
                </div>
              </CardHeader>
              <CardBody>
                <div className="table-responsive table-card">
                  {isLoading ? (
                    <div className="text-center p-5">
                      <Spinner color="primary" />
                      <p className="mt-2 text-muted text-uppercase fw-semibold fs-12">Fetching Fleet Data...</p>
                    </div>
                  ) : isError ? (
                    <div className="text-center p-5 text-danger">
                      <i className="ri-error-warning-line display-5"></i>
                      <p className="mt-2">Failed to load agents.</p>
                    </div>
                  ) : (
                    <Table className="align-middle table-nowrap mb-0">
                      <thead className="table-light">
                        <tr className="text-muted text-uppercase tracking-wide fs-11 fw-semibold">
                          <th scope="col">Status</th>
                          <th scope="col">Agent Identifier</th>
                          <th scope="col">Network</th>
                          <th scope="col">Location</th>
                          <th scope="col">Operating System</th>
                          <th scope="col">Last Seen</th>
                          <th scope="col" className="text-end">Actions</th>
                        </tr>
                      </thead>
                      <tbody>
                        {data?.agents.map((agent) => (
                          <tr key={agent.agent_id}>
                            <td>
                              <div className="d-flex align-items-center gap-2">
                                <div 
                                  className={`rounded-circle ${agent.online ? 'bg-success pulse-success' : 'bg-secondary'}`} 
                                  style={{ width: '8px', height: '8px', boxShadow: agent.online ? '0 0 10px rgba(34,197,94,0.5)' : 'none' }} 
                                />
                                <span className={`fs-12 fw-semibold text-uppercase ${agent.online ? 'text-success' : 'text-muted'}`}>
                                  {agent.online ? 'Online' : 'Offline'}
                                </span>
                              </div>
                            </td>
                            <td>
                              <div className="d-flex align-items-center gap-3">
                                <div className="avatar-sm flex-shrink-0">
                                  <div className="avatar-title bg-primary-subtle text-primary rounded-3 fs-18">
                                    <i className="ri-shield-star-line fs-20"></i>
                                  </div>
                                </div>
                                <div className="flex-grow-1">
                                  <h6 className="fs-14 mb-1">{agent.hostname}</h6>
                                  <p className="text-muted fs-11 font-monospace mb-0 tracking-tight">{agent.agent_id.substring(0, 8)}... | {agent.agent_version}</p>
                                </div>
                              </div>
                            </td>
                            <td>
                              <p className="fs-13 font-monospace mb-1">{agent.ip_address} <span className="text-muted fs-11">({agent.mac_address || 'NO MAC'})</span></p>
                              {agent.public_ip && <p className="text-primary fs-12 mb-0 font-monospace">Pub: {agent.public_ip}</p>}
                            </td>
                            <td>
                              {agent.geo ? (
                                <>
                                  <h6 className="fs-13 mb-1">{countryFlag(agent.geo.country_code)} {agent.geo.city}, {agent.geo.country_code}</h6>
                                  <p className="text-muted fs-11 mb-0 text-truncate" style={{maxWidth: '120px'}}>{agent.geo.isp}</p>
                                </>
                              ) : (
                                <p className="text-muted fs-12 mb-0">—</p>
                              )}
                            </td>
                            <td>
                              <p className="fs-14 text-body mb-1">{agent.os_type}</p>
                              <p className="text-muted fs-12 mb-0">{agent.os_version}</p>
                            </td>
                            <td>
                              <p className="text-muted fs-12 mb-0">{new Date(agent.last_seen_at).toLocaleString()}</p>
                            </td>
                            <td className="text-end">
                              <div className="d-flex align-items-center justify-content-end gap-2">
                                <Link 
                                  to={`/agents/${agent.agent_id}`}
                                  className="btn btn-sm btn-soft-primary btn-icon"
                                  title="Open Control Panel"
                                >
                                  <i className="ri-external-link-line fs-14"></i>
                                </Link>
                                <UncontrolledDropdown>
                                  <DropdownToggle color="light" size="sm" className="btn-icon text-muted" tag="button">
                                    <i className="ri-more-2-fill fs-14"></i>
                                  </DropdownToggle>
                                  <DropdownMenu className="dropdown-menu-end">
                                    <DropdownItem onClick={() => handleDeleteAgent(agent.agent_id)} className="text-danger">
                                      <i className="ri-delete-bin-line fs-14 me-2"></i> Delete Agent
                                    </DropdownItem>
                                  </DropdownMenu>
                                </UncontrolledDropdown>
                              </div>
                            </td>
                          </tr>
                        ))}
                        {data?.agents.length === 0 && (
                          <tr>
                            <td colSpan={6} className="text-center p-4 text-muted">
                              No agents found.
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
        </Row>
      </Container>
    </div>
  );
};
