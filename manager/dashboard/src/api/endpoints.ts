import api from './client';

export interface Agent {
  agent_id: string;
  hostname: string;
  os_type: string;
  os_version: string;
  agent_version: string;
  status: string;
  ip_address: string;
  registered_at: string;
  last_seen_at: string;
  online: boolean;
}

export interface SettingsResponse {
  max_agents: number;
  current_agent_count: number;
  success?: boolean;
}

export const endpoints = {
  health: () => api.get('/health'),
  login: (credentials: any) => api.post('/auth/login', credentials),
  
  agents: {
    list: () => api.get<{ agents: Agent[], count: number, max_agents: number, limit_reached: boolean }>('/agents'),
    get: (id: string) => api.get<Agent>(`/agents/${id}`),
    delete: (id: string) => api.delete(`/agents/${id}`),
    status: (id: string) => api.get(`/agents/${id}/status`),
    sendModuleCommand: (id: string, verb: string, params: any = {}) => 
      api.post(`/agents/${id}/module-command`, { verb, params }),
    sendPolicy: (id: string, type: string, data: any) => 
      api.post(`/agents/${id}/policy`, { policy_type: type, policy_data: data }),
    
    // Endpoint Management Native Commands
    endpoint: {
      users: {
        list: (id: string) => api.get(`/agents/${id}/endpoint/users`),
        create: (id: string, params: any) => api.post(`/agents/${id}/endpoint/users`, params),
        delete: (id: string, username: string) => api.delete(`/agents/${id}/endpoint/users/${username}`),
        enable: (id: string, username: string) => api.post(`/agents/${id}/endpoint/users/${username}/enable`),
        disable: (id: string, username: string) => api.post(`/agents/${id}/endpoint/users/${username}/disable`),
        unlock: (id: string, username: string) => api.post(`/agents/${id}/endpoint/users/${username}/unlock`),
        changePassword: (id: string, username: string, params: any) => api.post(`/agents/${id}/endpoint/users/${username}/password`, params),
      },
      groups: {
        list: (id: string) => api.get(`/agents/${id}/endpoint/groups`),
        addUser: (id: string, groupname: string, username: string) => api.post(`/agents/${id}/endpoint/groups/${groupname}/users/${username}`),
        removeUser: (id: string, groupname: string, username: string) => api.delete(`/agents/${id}/endpoint/groups/${groupname}/users/${username}`),
      },
      sessions: {
        list: (id: string) => api.get(`/agents/${id}/endpoint/sessions`),
        logoff: (id: string, sessionId: number) => api.post(`/agents/${id}/endpoint/sessions/${sessionId}/logoff`),
        disconnect: (id: string, sessionId: number) => api.post(`/agents/${id}/endpoint/sessions/${sessionId}/disconnect`),
        lockWorkstation: (id: string) => api.post(`/agents/${id}/endpoint/sessions/lock_workstation`),
      },
      inventory: {
        collect: (id: string) => api.get(`/agents/${id}/endpoint/inventory`),
      },
      passwordPolicy: {
        get: (id: string) => api.get(`/agents/${id}/endpoint/password-policy`),
        set: (id: string, params: any) => api.post(`/agents/${id}/endpoint/password-policy`, params),
      }
    }
  },
  
  commands: {
    listModule: (params: any) => api.get('/commands/module', { params }),
    listPolicy: (params: any) => api.get('/commands/policy', { params }),
    get: (id: string) => api.get(`/commands/${id}`),
  },
  
  audit: {
    list: (params: any) => api.get('/audit-log', { params }),
  },

  settings: {
    get: () => api.get<SettingsResponse>('/settings'),
    update: (data: Partial<{ max_agents: number }>) => api.put<SettingsResponse>('/settings', data),
  },
};
