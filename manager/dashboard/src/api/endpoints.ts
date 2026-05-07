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

export const endpoints = {
  health: () => api.get('/health'),
  login: (credentials: any) => api.post('/auth/login', credentials),
  
  agents: {
    list: () => api.get<{ agents: Agent[], count: number }>('/agents'),
    get: (id: string) => api.get<Agent>(`/agents/${id}`),
    status: (id: string) => api.get(`/agents/${id}/status`),
    sendModuleCommand: (id: string, verb: string, params: any = {}) => 
      api.post(`/agents/${id}/module-command`, { verb, params }),
    sendPolicy: (id: string, type: string, data: any) => 
      api.post(`/agents/${id}/policy`, { policy_type: type, policy_data: data }),
  },
  
  commands: {
    listModule: (params: any) => api.get('/commands/module', { params }),
    listPolicy: (params: any) => api.get('/commands/policy', { params }),
    get: (id: string) => api.get(`/commands/${id}`),
  },
  
  audit: {
    list: (params: any) => api.get('/audit-log', { params }),
  }
};
