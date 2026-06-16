import React, { useState } from 'react';
import { endpoints } from '../api/endpoints';
import { FolderTree, Cpu, RefreshCw, LogOut, Plus, Trash2, UserCheck, UserX, Save, X, Settings2, Key, Unplug } from 'lucide-react';
import { Card, CardBody, CardHeader, Row, Col, Button, Input, Spinner, Badge } from 'reactstrap';

interface EndpointTabProps {
  agentId: string;
}

const EndpointTab: React.FC<EndpointTabProps> = ({ agentId }) => {
  const [users, setUsers] = useState<any[]>([]);
  const [groups, setGroups] = useState<any[]>([]);
  const [sessions, setSessions] = useState<any[]>([]);
  
  // Password Policy State
  const [passwordPolicy, setPasswordPolicy] = useState<any>(null);
  const [isEditingPolicy, setIsEditingPolicy] = useState(false);
  const [editPolicy, setEditPolicy] = useState<any>({});
  
  // User Creation State
  const [showCreateUser, setShowCreateUser] = useState(false);
  const [newUser, setNewUser] = useState({ username: '', full_name: '', password: '' });
  
  // User Password Change State
  const [changePasswordUser, setChangePasswordUser] = useState<string | null>(null);
  const [newPassword, setNewPassword] = useState('');
  
  // Group Management State
  const [manageGroup, setManageGroup] = useState<string | null>(null);
  const [groupUser, setGroupUser] = useState('');

  const [inventory, setInventory] = useState<any>(null);

  const [loading, setLoading] = useState<Record<string, boolean>>({});
  const [message, setMessage] = useState<{type: 'success' | 'error', text: string} | null>(null);

  const showMessage = (type: 'success' | 'error', text: string) => {
    setMessage({ type, text });
    setTimeout(() => setMessage(null), 5000);
  };

  const executeCommand = async (type: string, promise: Promise<any>, onSuccess?: (data: any) => void) => {
    setLoading(prev => ({ ...prev, [type]: true }));
    try {
      const res = await promise;
      if (res.data?.status === 'queued') {
        showMessage('success', `Command queued: ${res.data.command_id}`);
        let attempts = 0;
        const poll = async () => {
          if (attempts > 30) {
            showMessage('error', 'Command timed out after 30 seconds');
            setLoading(prev => ({ ...prev, [type]: false }));
            return;
          }
          try {
            const statusRes = await endpoints.commands.get(res.data.command_id);
            const statusData = statusRes.data;
            if (statusData.status === 'acked') {
              showMessage('success', `Command completed successfully`);
              if (onSuccess && statusData.result_payload) {
                onSuccess(statusData.result_payload);
              }
              setLoading(prev => ({ ...prev, [type]: false }));
            } else if (statusData.status === 'failed') {
              showMessage('error', `Command failed: ${statusData.ack_status}`);
              setLoading(prev => ({ ...prev, [type]: false }));
            } else {
              attempts++;
              setTimeout(poll, 1000);
            }
          } catch (e) {
            showMessage('error', 'Failed to poll command status');
            setLoading(prev => ({ ...prev, [type]: false }));
          }
        };
        setTimeout(poll, 1000);
      } else {
        if (onSuccess) onSuccess(res.data);
        setLoading(prev => ({ ...prev, [type]: false }));
      }
    } catch (err: any) {
      showMessage('error', err.response?.data?.error || err.message || 'An error occurred');
      setLoading(prev => ({ ...prev, [type]: false }));
    }
  };

  const fetchUsers = () => executeCommand('users', endpoints.agents.endpoint.users.list(agentId), (data) => {
    try { setUsers(typeof data === 'string' ? JSON.parse(data) : (Array.isArray(data) ? data : [])); } catch(e) { console.error(e); }
  });
  const fetchGroups = () => executeCommand('groups', endpoints.agents.endpoint.groups.list(agentId), (data) => {
    try { setGroups(typeof data === 'string' ? JSON.parse(data) : (Array.isArray(data) ? data : [])); } catch(e) { console.error(e); }
  });
  const fetchSessions = () => executeCommand('sessions', endpoints.agents.endpoint.sessions.list(agentId), (data) => {
    try { setSessions(typeof data === 'string' ? JSON.parse(data) : (Array.isArray(data) ? data : [])); } catch(e) { console.error(e); }
  });
  const fetchPasswordPolicy = () => executeCommand('passwordPolicy', endpoints.agents.endpoint.passwordPolicy.get(agentId), (data) => {
    try { 
      const parsed = typeof data === 'string' ? JSON.parse(data) : data;
      setPasswordPolicy(parsed);
      setEditPolicy(parsed);
    } catch(e) { console.error(e); }
  });

  const handleCreateUser = () => {
    if (!newUser.username || !newUser.password) { showMessage('error', 'Username and password are required'); return; }
    executeCommand('createUser', endpoints.agents.endpoint.users.create(agentId, newUser), () => {
      setShowCreateUser(false); setNewUser({ username: '', full_name: '', password: '' }); fetchUsers();
    });
  };

  const handleDeleteUser = (username: string) => {
    if (confirm(`Are you sure you want to delete user ${username}?`)) {
      executeCommand('deleteUser', endpoints.agents.endpoint.users.delete(agentId, username), () => fetchUsers());
    }
  };

  const handleToggleUser = (username: string, isEnabled: boolean) => {
    const action = isEnabled ? endpoints.agents.endpoint.users.disable(agentId, username) : endpoints.agents.endpoint.users.enable(agentId, username);
    executeCommand('toggleUser', action, () => fetchUsers());
  };

  const handleChangePassword = (username: string) => {
    if (!newPassword) { showMessage('error', 'New password is required'); return; }
    executeCommand('changePassword', endpoints.agents.endpoint.users.changePassword(agentId, username, { password: newPassword }), () => {
      setChangePasswordUser(null); setNewPassword(''); showMessage('success', `Password changed for ${username}`);
    });
  };

  const handleAddUserToGroup = (groupname: string) => {
    if (!groupUser) return;
    executeCommand('manageGroup', endpoints.agents.endpoint.groups.addUser(agentId, groupname, groupUser), () => {
      setGroupUser(''); showMessage('success', `Added ${groupUser} to ${groupname}`);
    });
  };

  const handleRemoveUserFromGroup = (groupname: string) => {
    if (!groupUser) return;
    if (confirm(`Remove ${groupUser} from ${groupname}?`)) {
      executeCommand('manageGroup', endpoints.agents.endpoint.groups.removeUser(agentId, groupname, groupUser), () => {
        setGroupUser(''); showMessage('success', `Removed ${groupUser} from ${groupname}`);
      });
    }
  };

  const savePasswordPolicy = () => {
    const params = {
      min_length: parseInt(editPolicy.min_length || '-1'), max_age_days: parseInt(editPolicy.max_age_days || '-1'),
      min_age_days: parseInt(editPolicy.min_age_days || '-1'), history_length: parseInt(editPolicy.history_length || '-1')
    };
    executeCommand('savePolicy', endpoints.agents.endpoint.passwordPolicy.set(agentId, params), () => {
      setIsEditingPolicy(false); fetchPasswordPolicy();
    });
  };

  const handleLogoff = (sessionId: number) => {
    if (confirm('Are you sure you want to log off this session?')) { executeCommand('logoff', endpoints.agents.endpoint.sessions.logoff(agentId, sessionId)); }
  };

  const handleDisconnect = (sessionId: number) => {
    if (confirm('Are you sure you want to disconnect this session?')) { executeCommand('disconnect', endpoints.agents.endpoint.sessions.disconnect(agentId, sessionId)); }
  };

  return (
    <div>
      {message && (
        <div className={`alert alert-${message.type === 'success' ? 'success' : 'danger'} alert-dismissible fade show`} role="alert">
          {message.text}
        </div>
      )}

      <Row>
        {/* User Management */}
        <Col md={6}>
          <Card>
            <CardHeader className="align-items-center d-flex">
              <h4 className="card-title mb-0 flex-grow-1">User Management <Badge color="light" className="text-muted ms-2">{users.length} users</Badge></h4>
              <div className="flex-shrink-0 d-flex gap-2">
                <Button color="primary" size="sm" onClick={() => setShowCreateUser(!showCreateUser)}>
                  <Plus size={14} /> Create
                </Button>
                <Button color="light" size="sm" onClick={fetchUsers} disabled={loading['users']}>
                  {loading['users'] ? <Spinner size="sm" /> : <RefreshCw size={14} />}
                </Button>
              </div>
            </CardHeader>
            <CardBody style={{ maxHeight: '350px', overflowY: 'auto' }}>
              {showCreateUser && (
                <div className="mb-4 p-3 bg-light rounded">
                  <Row className="g-3">
                    <Col sm={6}><Input placeholder="Username" value={newUser.username} onChange={(e) => setNewUser({...newUser, username: e.target.value})} /></Col>
                    <Col sm={6}><Input placeholder="Full Name" value={newUser.full_name} onChange={(e) => setNewUser({...newUser, full_name: e.target.value})} /></Col>
                    <Col sm={12}><Input type="password" placeholder="Password" value={newUser.password} onChange={(e) => setNewUser({...newUser, password: e.target.value})} /></Col>
                  </Row>
                  <div className="d-flex justify-content-end gap-2 mt-3">
                    <Button color="link" size="sm" onClick={() => setShowCreateUser(false)}>Cancel</Button>
                    <Button color="primary" size="sm" onClick={handleCreateUser} disabled={loading['createUser']}>Create</Button>
                  </div>
                </div>
              )}
              <div className="list-group">
                {users.length > 0 ? users.map((u, i) => (
                  <React.Fragment key={i}>
                    <div className="list-group-item list-group-item-action d-flex justify-content-between align-items-center">
                      <div>
                        <h6 className="mb-1">{u.username} <span className="text-muted ms-2 fw-normal">{u.full_name}</span></h6>
                        {u.is_locked && <Badge color="danger" className="me-1">Locked</Badge>}
                        <Badge color={u.is_enabled ? 'success' : 'secondary'}>{u.is_enabled ? 'Active' : 'Disabled'}</Badge>
                      </div>
                      <div>
                        <Button color="warning" outline size="sm" className="me-1 btn-icon" onClick={() => { setChangePasswordUser(changePasswordUser === u.username ? null : u.username); setNewPassword(''); }}><Key size={14} /></Button>
                        <Button color="info" outline size="sm" className="me-1 btn-icon" onClick={() => handleToggleUser(u.username, u.is_enabled)}>{u.is_enabled ? <UserX size={14}/> : <UserCheck size={14}/>}</Button>
                        <Button color="danger" outline size="sm" className="btn-icon" onClick={() => handleDeleteUser(u.username)}><Trash2 size={14}/></Button>
                      </div>
                    </div>
                    {changePasswordUser === u.username && (
                      <div className="list-group-item bg-light d-flex gap-2 p-2">
                        <Input bsSize="sm" type="password" placeholder="New Password" value={newPassword} onChange={(e) => setNewPassword(e.target.value)} />
                        <Button size="sm" color="warning" onClick={() => handleChangePassword(u.username)}>Save</Button>
                      </div>
                    )}
                  </React.Fragment>
                )) : <div className="text-center text-muted py-4">No users fetched yet</div>}
              </div>
            </CardBody>
          </Card>
        </Col>

        {/* Group Management */}
        <Col md={6}>
          <Card>
            <CardHeader className="align-items-center d-flex">
              <h4 className="card-title mb-0 flex-grow-1">Group Management <Badge color="light" className="text-muted ms-2">{groups.length} groups</Badge></h4>
              <div className="flex-shrink-0">
                <Button color="light" size="sm" onClick={fetchGroups} disabled={loading['groups']}>
                  {loading['groups'] ? <Spinner size="sm" /> : <RefreshCw size={14} />}
                </Button>
              </div>
            </CardHeader>
            <CardBody style={{ maxHeight: '350px', overflowY: 'auto' }}>
              <div className="list-group">
                {groups.length > 0 ? groups.map((g, i) => (
                  <div key={i} className="list-group-item flex-column align-items-start">
                    <div className="d-flex justify-content-between align-items-center w-100 mb-1">
                      <h6 className="mb-0"><FolderTree size={16} className="me-2 text-primary" />{g.groupname}</h6>
                      <Button color="light" size="sm" onClick={() => setManageGroup(manageGroup === g.groupname ? null : g.groupname)}>{manageGroup === g.groupname ? 'Cancel' : 'Manage'}</Button>
                    </div>
                    {manageGroup === g.groupname && (
                      <div className="mt-3 d-flex gap-2">
                        <Input bsSize="sm" placeholder="Username..." value={groupUser} onChange={(e) => setGroupUser(e.target.value)} />
                        <Button color="primary" size="sm" onClick={() => handleAddUserToGroup(g.groupname)}>Add</Button>
                        <Button color="danger" size="sm" onClick={() => handleRemoveUserFromGroup(g.groupname)}>Remove</Button>
                      </div>
                    )}
                  </div>
                )) : <div className="text-center text-muted py-4">No groups fetched yet</div>}
              </div>
            </CardBody>
          </Card>
        </Col>
      </Row>

      <Row>
        {/* Password Policy */}
        <Col md={6}>
          <Card>
            <CardHeader className="align-items-center d-flex">
              <h4 className="card-title mb-0 flex-grow-1">Password Policy</h4>
              <div className="flex-shrink-0 d-flex gap-2">
                {passwordPolicy && !isEditingPolicy && (
                  <Button color="warning" outline size="sm" onClick={() => setIsEditingPolicy(true)}><Settings2 size={14} /> Edit</Button>
                )}
                {isEditingPolicy && (
                  <>
                    <Button color="light" size="sm" onClick={() => { setIsEditingPolicy(false); setEditPolicy(passwordPolicy); }}><X size={14} /></Button>
                    <Button color="success" size="sm" onClick={savePasswordPolicy} disabled={loading['savePolicy']}>{loading['savePolicy'] ? <Spinner size="sm" /> : <Save size={14} />} Save</Button>
                  </>
                )}
                <Button color="light" size="sm" onClick={fetchPasswordPolicy} disabled={loading['passwordPolicy']}>
                  {loading['passwordPolicy'] ? <Spinner size="sm" /> : <RefreshCw size={14} />}
                </Button>
              </div>
            </CardHeader>
            <CardBody>
              {passwordPolicy ? (
                <Row className="g-4">
                  <Col sm={6}>
                    <p className="text-muted mb-1 text-uppercase fw-medium fs-11">Min Password Length</p>
                    {isEditingPolicy ? <Input bsSize="sm" type="number" value={editPolicy.min_length} onChange={e => setEditPolicy({...editPolicy, min_length: e.target.value})} /> : <h5 className="fs-16">{passwordPolicy.min_length} <small className="text-muted fs-12 fw-normal">chars</small></h5>}
                  </Col>
                  <Col sm={6}>
                    <p className="text-muted mb-1 text-uppercase fw-medium fs-11">Max Password Age</p>
                    {isEditingPolicy ? <Input bsSize="sm" type="number" value={editPolicy.max_age_days} onChange={e => setEditPolicy({...editPolicy, max_age_days: e.target.value})} /> : <h5 className="fs-16">{passwordPolicy.max_age_days === 0 ? 'Never' : passwordPolicy.max_age_days} <small className="text-muted fs-12 fw-normal">days</small></h5>}
                  </Col>
                  <Col sm={6}>
                    <p className="text-muted mb-1 text-uppercase fw-medium fs-11">Min Password Age</p>
                    {isEditingPolicy ? <Input bsSize="sm" type="number" value={editPolicy.min_age_days} onChange={e => setEditPolicy({...editPolicy, min_age_days: e.target.value})} /> : <h5 className="fs-16">{passwordPolicy.min_age_days} <small className="text-muted fs-12 fw-normal">days</small></h5>}
                  </Col>
                  <Col sm={6}>
                    <p className="text-muted mb-1 text-uppercase fw-medium fs-11">Password History</p>
                    {isEditingPolicy ? <Input bsSize="sm" type="number" value={editPolicy.history_length} onChange={e => setEditPolicy({...editPolicy, history_length: e.target.value})} /> : <h5 className="fs-16">{passwordPolicy.history_length} <small className="text-muted fs-12 fw-normal">passwords</small></h5>}
                  </Col>
                </Row>
              ) : <div className="text-center text-muted py-4">No policy fetched yet</div>}
            </CardBody>
          </Card>
        </Col>

        {/* Session Management */}
        <Col md={6}>
          <Card>
            <CardHeader className="align-items-center d-flex">
              <h4 className="card-title mb-0 flex-grow-1">Session Management <Badge color="light" className="text-muted ms-2">{sessions.length} sessions</Badge></h4>
              <div className="flex-shrink-0">
                <Button color="light" size="sm" onClick={fetchSessions} disabled={loading['sessions']}>
                  {loading['sessions'] ? <Spinner size="sm" /> : <RefreshCw size={14} />}
                </Button>
              </div>
            </CardHeader>
            <CardBody style={{ maxHeight: '350px', overflowY: 'auto' }}>
              <div className="list-group">
                {sessions.length > 0 ? sessions.map((s, i) => (
                  <div key={i} className="list-group-item d-flex justify-content-between align-items-center">
                    <div>
                      <h6 className="mb-1"><span className="text-muted me-2">#{s.session_id}</span>{s.username || 'System'}</h6>
                      <p className="text-muted mb-0 fs-11">{s.state} &bull; {s.station_name}</p>
                    </div>
                    <div>
                      <Button color="warning" outline size="sm" className="me-1 btn-icon" onClick={() => handleDisconnect(s.session_id)} title="Disconnect"><Unplug size={14}/></Button>
                      <Button color="danger" outline size="sm" className="btn-icon" onClick={() => handleLogoff(s.session_id)} title="Logoff"><LogOut size={14}/></Button>
                    </div>
                  </div>
                )) : <div className="text-center text-muted py-4">No sessions fetched yet</div>}
              </div>
            </CardBody>
          </Card>
        </Col>
      </Row>

      {/* Inventory Collection */}
      <Row>
        <Col xs={12}>
          <Card>
            <CardHeader className="align-items-center d-flex">
              <h4 className="card-title mb-0 flex-grow-1">System Inventory</h4>
              <div className="flex-shrink-0">
                <Button color="info" outline size="sm" onClick={() => executeCommand('inventory', endpoints.agents.endpoint.inventory.collect(agentId), (data) => setInventory(typeof data === 'string' ? JSON.parse(data) : data))} disabled={loading['inventory']}>
                  {loading['inventory'] ? <Spinner size="sm" /> : <><Cpu size={14} className="me-1"/> Collect</>}
                </Button>
              </div>
            </CardHeader>
            <CardBody>
              <p className="text-muted fs-13 mb-4">Triggers a deep hardware and software scan on the endpoint. Results include OS info, disk volumes, and network adapters.</p>
              
              {inventory && (
                <div className="bg-light p-4 rounded">
                  <h5 className="fs-14 fw-bold mb-3">System Information</h5>
                  <Row className="g-3 mb-4">
                    <Col sm={3}><div className="text-muted fs-12">Architecture</div><div className="fw-medium">{inventory.architecture || 'N/A'}</div></Col>
                    <Col sm={3}><div className="text-muted fs-12">Build</div><div className="fw-medium">{inventory.build || 'N/A'}</div></Col>
                    <Col sm={3}><div className="text-muted fs-12">Version</div><div className="fw-medium">{inventory.version || 'N/A'}</div></Col>
                    <Col sm={3}><div className="text-muted fs-12">Processors</div><div className="fw-medium">{inventory.num_processors || 'N/A'}</div></Col>
                  </Row>

                  {inventory.ip_addresses && inventory.ip_addresses.length > 0 && (
                    <>
                      <h5 className="fs-14 fw-bold mb-3 mt-4">Network Adapters</h5>
                      <div className="d-flex flex-column gap-2 mb-4">
                        {inventory.ip_addresses.map((ip: any, i: number) => (
                          <div key={i} className="d-flex justify-content-between border p-2 rounded bg-white">
                            <span className="fw-medium">{ip.adapter}</span>
                            <span><Badge color="info" className="me-2">{ip.version}</Badge><span className="font-monospace text-muted">{ip.ip}</span></span>
                          </div>
                        ))}
                      </div>
                    </>
                  )}

                  {inventory.logical_disks && inventory.logical_disks.length > 0 && (
                    <>
                      <h5 className="fs-14 fw-bold mb-3 mt-4">Logical Disks</h5>
                      <Row className="g-3">
                        {inventory.logical_disks.map((disk: any, i: number) => (
                          <Col md={6} key={i}>
                            <div className="border p-3 rounded bg-white">
                              <div className="d-flex justify-content-between mb-2">
                                <span className="fw-bold">{disk.drive}</span>
                                <Badge color="secondary">{disk.type}</Badge>
                              </div>
                              <div className="d-flex justify-content-between text-muted fs-12 mb-1">
                                <span>Free: {(disk.free_bytes / (1024 * 1024 * 1024)).toFixed(1)} GB</span>
                                <span>Total: {(disk.total_bytes / (1024 * 1024 * 1024)).toFixed(1)} GB</span>
                              </div>
                              <div className="progress progress-sm">
                                <div className="progress-bar bg-success" style={{ width: `${((disk.total_bytes - disk.free_bytes) / disk.total_bytes) * 100}%` }}></div>
                              </div>
                            </div>
                          </Col>
                        ))}
                      </Row>
                    </>
                  )}
                </div>
              )}
            </CardBody>
          </Card>
        </Col>
      </Row>
    </div>
  );
};

export default EndpointTab;
