import React, { useState } from 'react';
import { endpoints } from '../api/endpoints';
import { Users, Monitor, FolderTree, Cpu, RefreshCw, LogOut, Loader2, ShieldAlert, Plus, Trash2, UserCheck, UserX, Save, X, Settings2, Key, Unplug } from 'lucide-react';
import { cn } from '../lib/utils';

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
      // Wait for async command queuing if it returns command_id, otherwise data is immediate
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

  // Fetch functions
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

  // Action functions
  const handleCreateUser = () => {
    if (!newUser.username || !newUser.password) {
      showMessage('error', 'Username and password are required');
      return;
    }
    executeCommand('createUser', endpoints.agents.endpoint.users.create(agentId, newUser), () => {
      setShowCreateUser(false);
      setNewUser({ username: '', full_name: '', password: '' });
      fetchUsers();
    });
  };

  const handleDeleteUser = (username: string) => {
    if (confirm(`Are you sure you want to delete user ${username}?`)) {
      executeCommand('deleteUser', endpoints.agents.endpoint.users.delete(agentId, username), () => fetchUsers());
    }
  };

  const handleToggleUser = (username: string, isEnabled: boolean) => {
    const action = isEnabled 
      ? endpoints.agents.endpoint.users.disable(agentId, username) 
      : endpoints.agents.endpoint.users.enable(agentId, username);
    executeCommand('toggleUser', action, () => fetchUsers());
  };

  const handleChangePassword = (username: string) => {
    if (!newPassword) {
      showMessage('error', 'New password is required');
      return;
    }
    executeCommand('changePassword', endpoints.agents.endpoint.users.changePassword(agentId, username, { password: newPassword }), () => {
      setChangePasswordUser(null);
      setNewPassword('');
      showMessage('success', `Password changed for ${username}`);
    });
  };

  const handleAddUserToGroup = (groupname: string) => {
    if (!groupUser) return;
    executeCommand('manageGroup', endpoints.agents.endpoint.groups.addUser(agentId, groupname, groupUser), () => {
      setGroupUser('');
      showMessage('success', `Added ${groupUser} to ${groupname}`);
    });
  };

  const handleRemoveUserFromGroup = (groupname: string) => {
    if (!groupUser) return;
    if (confirm(`Remove ${groupUser} from ${groupname}?`)) {
      executeCommand('manageGroup', endpoints.agents.endpoint.groups.removeUser(agentId, groupname, groupUser), () => {
        setGroupUser('');
        showMessage('success', `Removed ${groupUser} from ${groupname}`);
      });
    }
  };

  const savePasswordPolicy = () => {
    const params = {
      min_length: parseInt(editPolicy.min_length || '-1'),
      max_age_days: parseInt(editPolicy.max_age_days || '-1'),
      min_age_days: parseInt(editPolicy.min_age_days || '-1'),
      history_length: parseInt(editPolicy.history_length || '-1')
    };
    executeCommand('savePolicy', endpoints.agents.endpoint.passwordPolicy.set(agentId, params), () => {
      setIsEditingPolicy(false);
      fetchPasswordPolicy();
    });
  };

  const handleLogoff = (sessionId: number) => {
    if (confirm('Are you sure you want to log off this session?')) {
      executeCommand('logoff', endpoints.agents.endpoint.sessions.logoff(agentId, sessionId));
    }
  };

  const handleDisconnect = (sessionId: number) => {
    if (confirm('Are you sure you want to disconnect this session?')) {
      executeCommand('disconnect', endpoints.agents.endpoint.sessions.disconnect(agentId, sessionId));
    }
  };

  const LoadingSpinner = () => <Loader2 className="w-3.5 h-3.5 animate-spin" />;

  const InputField = ({ label, value, onChange, type="text", placeholder="" }: any) => (
    <div>
      <label className="block text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-1.5">{label}</label>
      <input 
        type={type} 
        value={value} 
        onChange={onChange} 
        placeholder={placeholder}
        className="w-full bg-rn-black/40 border border-rn-white/10 rounded-xl px-3 py-2 text-sm text-rn-white focus:outline-none focus:border-rn-white/30 transition-all placeholder:text-rn-white/20"
      />
    </div>
  );

  return (
    <div className="space-y-6">
      {message && (
        <div className={cn(
          "flex items-center gap-3 px-5 py-3.5 rounded-2xl border text-sm font-medium animate-in fade-in slide-in-from-top-2 duration-300",
          message.type === 'success'
            ? 'bg-green-500/10 border-green-500/20 text-green-400'
            : 'bg-red-500/10 border-red-500/20 text-red-400'
        )}>
          <div className={cn(
            "w-2 h-2 rounded-full shrink-0",
            message.type === 'success' ? 'bg-green-500' : 'bg-red-500'
          )} />
          {message.text}
        </div>
      )}

      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        {/* User Management */}
        <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 transition-all hover:border-rn-white/10 flex flex-col h-[380px]">
          <div className="flex justify-between items-center mb-5 shrink-0">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-2xl bg-blue-500/10 flex items-center justify-center">
                <Users className="w-5 h-5 text-blue-400" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-rn-white uppercase tracking-wider">User Management</h3>
                <p className="text-[10px] text-rn-white/30 font-mono mt-0.5">{users.length > 0 ? `${users.length} users` : 'verb: user_list'}</p>
              </div>
            </div>
            <div className="flex gap-2">
              <button
                onClick={() => setShowCreateUser(!showCreateUser)}
                className="flex items-center gap-1.5 px-3 py-2 rounded-xl bg-blue-500/10 border border-blue-500/20 text-xs font-bold text-blue-400 hover:bg-blue-500/20 transition-all"
              >
                <Plus className="w-3.5 h-3.5" /> Create
              </button>
              <button
                onClick={fetchUsers}
                disabled={loading['users']}
                className="flex items-center gap-2 px-3 py-2 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/60 hover:text-rn-white hover:bg-rn-white/10 transition-all disabled:opacity-50"
              >
                {loading['users'] ? <LoadingSpinner /> : <RefreshCw className="w-3.5 h-3.5" />}
              </button>
            </div>
          </div>
          
          {showCreateUser && (
            <div className="mb-4 p-4 rounded-2xl bg-rn-white/[0.02] border border-rn-white/10 shrink-0">
              <div className="grid grid-cols-2 gap-3 mb-3">
                <InputField label="Username" value={newUser.username} onChange={(e: any) => setNewUser({...newUser, username: e.target.value})} />
                <InputField label="Full Name" value={newUser.full_name} onChange={(e: any) => setNewUser({...newUser, full_name: e.target.value})} />
                <div className="col-span-2">
                  <InputField label="Password" type="password" value={newUser.password} onChange={(e: any) => setNewUser({...newUser, password: e.target.value})} />
                </div>
              </div>
              <div className="flex justify-end gap-2">
                <button onClick={() => setShowCreateUser(false)} className="px-3 py-1.5 text-xs text-rn-white/50 hover:text-rn-white font-bold">Cancel</button>
                <button onClick={handleCreateUser} disabled={loading['createUser']} className="px-4 py-1.5 bg-blue-500 text-white text-xs font-bold rounded-lg hover:bg-blue-600 disabled:opacity-50 flex items-center gap-2">
                  {loading['createUser'] && <LoadingSpinner />} Create User
                </button>
              </div>
            </div>
          )}

          <div className="flex-1 overflow-y-auto custom-scrollbar pr-1 space-y-1.5">
            {users.length > 0 ? (
              users.map((u, i) => (
                <div key={i} className="flex flex-col px-4 py-3 rounded-xl bg-rn-white/[0.02] border border-rn-white/5 hover:bg-rn-white/[0.04] transition-all group">
                  <div className="flex justify-between items-center">
                    <div>
                      <span className="text-sm font-bold text-rn-white">{u.username}</span>
                      {u.full_name && <span className="text-xs text-rn-white/30 ml-2">{u.full_name}</span>}
                    </div>
                    <div className="flex items-center gap-2">
                      {u.is_locked && (
                        <span className="text-[9px] px-2 py-0.5 rounded-full bg-red-500/10 text-red-400 border border-red-500/20 font-bold uppercase tracking-widest">Locked</span>
                      )}
                      <span className={cn(
                        "text-[9px] px-2 py-0.5 rounded-full font-bold uppercase tracking-widest border",
                        u.is_enabled ? 'bg-green-500/10 text-green-400 border-green-500/20' : 'bg-rn-white/5 text-rn-white/30 border-rn-white/10'
                      )}>
                        {u.is_enabled ? 'Active' : 'Disabled'}
                      </span>
                    </div>
                  </div>
                  <div className="flex justify-end gap-2 mt-2 opacity-0 group-hover:opacity-100 transition-opacity">
                    <button 
                      onClick={() => {
                        setChangePasswordUser(changePasswordUser === u.username ? null : u.username);
                        setNewPassword('');
                      }}
                      className="p-1.5 rounded bg-amber-500/10 hover:bg-amber-500/20 text-amber-400 transition-all"
                      title="Change Password"
                    >
                      <Key className="w-3.5 h-3.5" />
                    </button>
                    <button 
                      onClick={() => handleToggleUser(u.username, u.is_enabled)}
                      className="p-1.5 rounded bg-rn-white/5 hover:bg-rn-white/10 text-rn-white/60 hover:text-rn-white transition-all"
                      title={u.is_enabled ? "Disable User" : "Enable User"}
                    >
                      {u.is_enabled ? <UserX className="w-3.5 h-3.5" /> : <UserCheck className="w-3.5 h-3.5" />}
                    </button>
                    <button 
                      onClick={() => handleDeleteUser(u.username)}
                      className="p-1.5 rounded bg-red-500/10 hover:bg-red-500/20 text-red-400 transition-all"
                      title="Delete User"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  </div>
                  {changePasswordUser === u.username && (
                    <div className="mt-3 pt-3 border-t border-rn-white/10 flex gap-2">
                      <input 
                        type="password"
                        value={newPassword}
                        onChange={(e) => setNewPassword(e.target.value)}
                        placeholder="New password..."
                        className="flex-1 bg-rn-black/40 border border-rn-white/10 rounded-lg px-3 py-1.5 text-xs text-rn-white placeholder:text-rn-white/20 focus:outline-none focus:border-rn-white/30"
                      />
                      <button onClick={() => handleChangePassword(u.username)} disabled={loading['changePassword']} className="px-3 py-1.5 bg-amber-500/20 text-amber-400 text-xs font-bold rounded-lg hover:bg-amber-500/30 transition-all">
                        Update
                      </button>
                    </div>
                  )}
                </div>
              ))
            ) : (
              <div className="h-full flex items-center justify-center text-rn-white/15 text-xs font-bold uppercase tracking-widest border-2 border-dashed border-rn-white/5 rounded-2xl">
                No users fetched yet
              </div>
            )}
          </div>
        </div>

        {/* Group Management */}
        <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 transition-all hover:border-rn-white/10 flex flex-col h-[380px]">
          <div className="flex justify-between items-center mb-5 shrink-0">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-2xl bg-purple-500/10 flex items-center justify-center">
                <FolderTree className="w-5 h-5 text-purple-400" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-rn-white uppercase tracking-wider">Group Management</h3>
                <p className="text-[10px] text-rn-white/30 font-mono mt-0.5">{groups.length > 0 ? `${groups.length} groups` : 'verb: group_list'}</p>
              </div>
            </div>
            <button
              onClick={fetchGroups}
              disabled={loading['groups']}
              className="flex items-center gap-2 px-3 py-2 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/60 hover:text-rn-white hover:bg-rn-white/10 transition-all disabled:opacity-50"
            >
              {loading['groups'] ? <LoadingSpinner /> : <RefreshCw className="w-3.5 h-3.5" />}
            </button>
          </div>
          
          <div className="flex-1 overflow-y-auto custom-scrollbar pr-1 space-y-1.5">
            {groups.length > 0 ? (
              groups.map((g, i) => (
                <div key={i} className="flex flex-col px-4 py-2.5 rounded-xl bg-rn-white/[0.02] border border-rn-white/5 hover:bg-rn-white/[0.04] transition-all">
                  <div className="flex justify-between items-center">
                    <div className="flex items-center gap-3">
                      <div className="w-6 h-6 rounded-lg bg-purple-500/10 flex items-center justify-center">
                        <FolderTree className="w-3 h-3 text-purple-400/60" />
                      </div>
                      <span className="text-sm font-bold text-rn-white/90">{g.groupname}</span>
                    </div>
                    <button 
                      onClick={() => {
                        setManageGroup(manageGroup === g.groupname ? null : g.groupname);
                        setGroupUser('');
                      }}
                      className="px-2 py-1 bg-rn-white/5 hover:bg-rn-white/10 rounded text-xs text-rn-white/60 hover:text-rn-white transition-all font-bold"
                    >
                      {manageGroup === g.groupname ? 'Cancel' : 'Manage'}
                    </button>
                  </div>
                  
                  {manageGroup === g.groupname && (
                    <div className="mt-3 pt-3 border-t border-rn-white/10 flex gap-2">
                      <input 
                        type="text"
                        value={groupUser}
                        onChange={(e) => setGroupUser(e.target.value)}
                        placeholder="Enter username..."
                        className="flex-1 bg-rn-black/40 border border-rn-white/10 rounded-lg px-3 py-1.5 text-xs text-rn-white placeholder:text-rn-white/20 focus:outline-none focus:border-rn-white/30"
                      />
                      <button onClick={() => handleAddUserToGroup(g.groupname)} disabled={loading['manageGroup']} className="px-3 py-1.5 bg-purple-500/20 text-purple-400 text-xs font-bold rounded-lg hover:bg-purple-500/30 transition-all">
                        Add
                      </button>
                      <button onClick={() => handleRemoveUserFromGroup(g.groupname)} disabled={loading['manageGroup']} className="px-3 py-1.5 bg-red-500/10 text-red-400 text-xs font-bold rounded-lg hover:bg-red-500/20 transition-all">
                        Remove
                      </button>
                    </div>
                  )}
                </div>
              ))
            ) : (
              <div className="h-full flex items-center justify-center text-rn-white/15 text-xs font-bold uppercase tracking-widest border-2 border-dashed border-rn-white/5 rounded-2xl">
                No groups fetched yet
              </div>
            )}
          </div>
        </div>

        {/* Password Policy */}
        <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 transition-all hover:border-rn-white/10 flex flex-col">
          <div className="flex justify-between items-center mb-5 shrink-0">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-2xl bg-amber-500/10 flex items-center justify-center">
                <ShieldAlert className="w-5 h-5 text-amber-400" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-rn-white uppercase tracking-wider">Password Policy</h3>
                <p className="text-[10px] text-rn-white/30 font-mono mt-0.5">verb: password_policy</p>
              </div>
            </div>
            <div className="flex gap-2">
              {passwordPolicy && !isEditingPolicy && (
                <button
                  onClick={() => setIsEditingPolicy(true)}
                  className="flex items-center gap-1.5 px-3 py-2 rounded-xl bg-amber-500/10 border border-amber-500/20 text-xs font-bold text-amber-400 hover:bg-amber-500/20 transition-all"
                >
                  <Settings2 className="w-3.5 h-3.5" /> Edit
                </button>
              )}
              {isEditingPolicy && (
                <>
                  <button onClick={() => { setIsEditingPolicy(false); setEditPolicy(passwordPolicy); }} className="px-3 py-2 rounded-xl bg-rn-white/5 text-xs font-bold text-rn-white/50 hover:bg-rn-white/10 hover:text-rn-white">
                    <X className="w-3.5 h-3.5" />
                  </button>
                  <button onClick={savePasswordPolicy} disabled={loading['savePolicy']} className="flex items-center gap-1.5 px-3 py-2 rounded-xl bg-green-500/20 text-xs font-bold text-green-400 hover:bg-green-500/30">
                    {loading['savePolicy'] ? <LoadingSpinner /> : <Save className="w-3.5 h-3.5" />} Save
                  </button>
                </>
              )}
              <button
                onClick={fetchPasswordPolicy}
                disabled={loading['passwordPolicy'] || isEditingPolicy}
                className="flex items-center gap-2 px-3 py-2 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/60 hover:text-rn-white hover:bg-rn-white/10 transition-all disabled:opacity-50"
              >
                {loading['passwordPolicy'] ? <LoadingSpinner /> : <RefreshCw className="w-3.5 h-3.5" />}
              </button>
            </div>
          </div>
          
          <div className="flex-1 bg-rn-white/[0.02] border border-rn-white/5 rounded-2xl p-5">
            {passwordPolicy ? (
              <div className="grid grid-cols-2 gap-y-6 gap-x-4">
                <div>
                  <div className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-1.5">Min Password Length</div>
                  {isEditingPolicy ? (
                    <input type="number" className="w-full bg-rn-black/40 border border-rn-white/10 rounded-lg px-2 py-1 text-sm text-rn-white focus:outline-none focus:border-rn-white/30" 
                      value={editPolicy.min_length} onChange={e => setEditPolicy({...editPolicy, min_length: e.target.value})} />
                  ) : (
                    <div className="text-lg font-bold text-rn-white">{passwordPolicy.min_length} <span className="text-xs text-rn-white/30 font-normal">chars</span></div>
                  )}
                </div>
                <div>
                  <div className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-1.5">Max Password Age</div>
                  {isEditingPolicy ? (
                    <input type="number" className="w-full bg-rn-black/40 border border-rn-white/10 rounded-lg px-2 py-1 text-sm text-rn-white focus:outline-none focus:border-rn-white/30" 
                      value={editPolicy.max_age_days} onChange={e => setEditPolicy({...editPolicy, max_age_days: e.target.value})} />
                  ) : (
                    <div className="text-lg font-bold text-rn-white">{passwordPolicy.max_age_days === 0 ? 'Never' : passwordPolicy.max_age_days} <span className="text-xs text-rn-white/30 font-normal">days</span></div>
                  )}
                </div>
                <div>
                  <div className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-1.5">Min Password Age</div>
                  {isEditingPolicy ? (
                    <input type="number" className="w-full bg-rn-black/40 border border-rn-white/10 rounded-lg px-2 py-1 text-sm text-rn-white focus:outline-none focus:border-rn-white/30" 
                      value={editPolicy.min_age_days} onChange={e => setEditPolicy({...editPolicy, min_age_days: e.target.value})} />
                  ) : (
                    <div className="text-lg font-bold text-rn-white">{passwordPolicy.min_age_days} <span className="text-xs text-rn-white/30 font-normal">days</span></div>
                  )}
                </div>
                <div>
                  <div className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-1.5">Password History</div>
                  {isEditingPolicy ? (
                    <input type="number" className="w-full bg-rn-black/40 border border-rn-white/10 rounded-lg px-2 py-1 text-sm text-rn-white focus:outline-none focus:border-rn-white/30" 
                      value={editPolicy.history_length} onChange={e => setEditPolicy({...editPolicy, history_length: e.target.value})} />
                  ) : (
                    <div className="text-lg font-bold text-rn-white">{passwordPolicy.history_length} <span className="text-xs text-rn-white/30 font-normal">passwords</span></div>
                  )}
                </div>
              </div>
            ) : (
              <div className="h-full flex items-center justify-center text-rn-white/15 text-xs font-bold uppercase tracking-widest">
                No policy fetched yet
              </div>
            )}
          </div>
        </div>

        {/* Session Management */}
        <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 transition-all hover:border-rn-white/10 flex flex-col">
          <div className="flex justify-between items-center mb-5 shrink-0">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-2xl bg-indigo-500/10 flex items-center justify-center">
                <Monitor className="w-5 h-5 text-indigo-400" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-rn-white uppercase tracking-wider">Session Management</h3>
                <p className="text-[10px] text-rn-white/30 font-mono mt-0.5">{sessions.length > 0 ? `${sessions.length} sessions` : 'verb: session_list'}</p>
              </div>
            </div>
            <button
              onClick={fetchSessions}
              disabled={loading['sessions']}
              className="flex items-center gap-2 px-3 py-2 rounded-xl bg-rn-white/5 border border-rn-white/10 text-xs font-bold text-rn-white/60 hover:text-rn-white hover:bg-rn-white/10 transition-all disabled:opacity-50"
            >
              {loading['sessions'] ? <LoadingSpinner /> : <RefreshCw className="w-3.5 h-3.5" />}
            </button>
          </div>
          
          <div className="flex-1 overflow-y-auto custom-scrollbar pr-1 space-y-1.5 min-h-[150px]">
            {sessions.length > 0 ? (
              sessions.map((s, i) => (
                <div key={i} className="flex justify-between items-center px-4 py-2.5 rounded-xl bg-rn-white/[0.02] border border-rn-white/5 hover:bg-rn-white/[0.04] transition-all">
                  <div>
                    <div className="text-sm font-bold text-rn-white">
                      <span className="text-rn-white/40 font-mono text-xs mr-2">#{s.session_id}</span>
                      {s.username || 'System'}
                    </div>
                    <div className="text-[10px] text-rn-white/30 font-mono mt-0.5">{s.state} • {s.station_name}</div>
                  </div>
                  <div className="flex items-center gap-2">
                    <button
                      onClick={() => handleDisconnect(s.session_id)}
                      className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[10px] font-bold uppercase tracking-widest text-orange-400/70 border border-orange-500/20 bg-orange-500/5 hover:bg-orange-500/10 hover:text-orange-400 hover:border-orange-500/30 transition-all"
                    >
                      <Unplug className="w-3 h-3" />
                      Disconnect
                    </button>
                    <button
                      onClick={() => handleLogoff(s.session_id)}
                      className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[10px] font-bold uppercase tracking-widest text-red-400/70 border border-red-500/20 bg-red-500/5 hover:bg-red-500/10 hover:text-red-400 hover:border-red-500/30 transition-all"
                    >
                      <LogOut className="w-3 h-3" />
                      Logoff
                    </button>
                  </div>
                </div>
              ))
            ) : (
              <div className="h-full flex items-center justify-center text-rn-white/15 text-xs font-bold uppercase tracking-widest border-2 border-dashed border-rn-white/5 rounded-2xl">
                No sessions fetched yet
              </div>
            )}
          </div>
        </div>

        {/* Inventory Collection */}
        <div className="md:col-span-2 bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 transition-all hover:border-rn-white/10">
          <div className="flex justify-between items-center mb-5">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-2xl bg-emerald-500/10 flex items-center justify-center">
                <Cpu className="w-5 h-5 text-emerald-400" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-rn-white uppercase tracking-wider">System Inventory</h3>
                <p className="text-[10px] text-rn-white/30 font-mono mt-0.5">verb: inventory_collect</p>
              </div>
            </div>
            <button
              onClick={() => executeCommand('inventory', endpoints.agents.endpoint.inventory.collect(agentId), (data) => setInventory(typeof data === 'string' ? JSON.parse(data) : data))}
              disabled={loading['inventory']}
              className="flex items-center gap-2 px-4 py-2 rounded-xl bg-emerald-500/10 border border-emerald-500/20 text-xs font-bold text-emerald-400/80 hover:text-emerald-400 hover:bg-emerald-500/15 hover:border-emerald-500/30 transition-all disabled:opacity-50"
            >
              {loading['inventory'] ? <LoadingSpinner /> : <Cpu className="w-3.5 h-3.5" />}
              {loading['inventory'] ? 'Queuing...' : 'Collect'}
            </button>
          </div>
          <p className="text-xs text-rn-white/30 leading-relaxed mb-4">
            Triggers a deep hardware and software scan on the endpoint. Results include OS info, disk volumes, and network adapters. Data is queued and pushed back asynchronously.
          </p>
          
          {inventory && (
            <div className="bg-rn-white/[0.02] border border-rn-white/5 rounded-2xl p-5 space-y-6 animate-in fade-in slide-in-from-top-2">
              {/* OS Info */}
              <div>
                <h4 className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-3">System Information</h4>
                <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                  <div>
                    <div className="text-[10px] text-rn-white/30">Architecture</div>
                    <div className="text-sm font-bold text-rn-white">{inventory.architecture || 'N/A'}</div>
                  </div>
                  <div>
                    <div className="text-[10px] text-rn-white/30">Build</div>
                    <div className="text-sm font-bold text-rn-white">{inventory.build || 'N/A'}</div>
                  </div>
                  <div>
                    <div className="text-[10px] text-rn-white/30">Version</div>
                    <div className="text-sm font-bold text-rn-white">{inventory.version || 'N/A'}</div>
                  </div>
                  <div>
                    <div className="text-[10px] text-rn-white/30">Processors</div>
                    <div className="text-sm font-bold text-rn-white">{inventory.num_processors || 'N/A'}</div>
                  </div>
                </div>
              </div>

              {/* Network Adapters */}
              {inventory.ip_addresses && inventory.ip_addresses.length > 0 && (
                <div>
                  <h4 className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-3">Network Adapters</h4>
                  <div className="space-y-2">
                    {inventory.ip_addresses.map((ip: any, i: number) => (
                      <div key={i} className="flex justify-between items-center px-3 py-2 rounded-xl bg-rn-white/[0.02] border border-rn-white/5">
                        <span className="text-xs font-medium text-rn-white/70">{ip.adapter}</span>
                        <div className="flex items-center gap-2">
                          <span className="text-[9px] px-1.5 py-0.5 rounded bg-emerald-500/10 text-emerald-400 font-bold uppercase tracking-widest">{ip.version}</span>
                          <span className="text-sm font-mono text-rn-white">{ip.ip}</span>
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              )}

              {/* Logical Disks */}
              {inventory.logical_disks && inventory.logical_disks.length > 0 && (
                <div>
                  <h4 className="text-[10px] font-bold text-rn-white/40 uppercase tracking-wider mb-3">Logical Disks</h4>
                  <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                    {inventory.logical_disks.map((disk: any, i: number) => (
                      <div key={i} className="px-4 py-3 rounded-xl bg-rn-white/[0.02] border border-rn-white/5">
                        <div className="flex justify-between items-center mb-2">
                          <span className="text-sm font-bold text-rn-white">{disk.drive}</span>
                          <span className="text-[9px] px-1.5 py-0.5 rounded bg-rn-white/10 text-rn-white/60 font-bold uppercase tracking-widest">{disk.type}</span>
                        </div>
                        <div className="flex justify-between text-xs text-rn-white/50 mb-1">
                          <span>Free: {(disk.free_bytes / (1024 * 1024 * 1024)).toFixed(1)} GB</span>
                          <span>Total: {(disk.total_bytes / (1024 * 1024 * 1024)).toFixed(1)} GB</span>
                        </div>
                        <div className="w-full h-1.5 bg-rn-white/5 rounded-full overflow-hidden">
                          <div 
                            className="h-full bg-emerald-400 rounded-full" 
                            style={{ width: `${((disk.total_bytes - disk.free_bytes) / disk.total_bytes) * 100}%` }}
                          />
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              )}
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

export default EndpointTab;
