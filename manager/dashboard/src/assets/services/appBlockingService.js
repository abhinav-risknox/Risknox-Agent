// ============================================
// frontend/src/services/appBlockingService.js
// ============================================
import apiService from './apiService';

class AppBlockingService {
  /**
   * Block an application on specific agents
   */
  async blockApplication(appData) {
    try {
      const { name, executable, agent_ids } = appData;
      
      // Validate required fields
      if (!name || !executable) {
        throw new Error('Application name and executable are required');
      }
      
      // If no specific agents provided, get all online agents
      let targetAgents = agent_ids;
      if (!targetAgents || targetAgents.length === 0) {
        const agentsResponse = await apiService.get('/api/agents');
        targetAgents = agentsResponse.agents
          .filter(a => a.status === 'online')
          .map(a => a.agent_id);
      }
      
      if (targetAgents.length === 0) {
        throw new Error('No online agents available');
      }
      
      const response = await apiService.post('/api/app-blocking/block', {
        agent_ids: targetAgents,
        name: name,
        executable: executable
      });
      
      return {
        success: true,
        message: `Application blocking initiated on ${response.count} agent(s)`,
        command_ids: response.command_ids,
        count: response.count,
        online_agents: response.online_agents,
        offline_agents: response.offline_agents
      };
    } catch (error) {
      console.error('Failed to block application:', error);
      throw error;
    }
  }

  /**
   * Unblock an application on specific agents
   */
  async unblockApplication(appData) {
    try {
      const { executable, agent_ids } = appData;
      
      // Validate required fields
      if (!executable) {
        throw new Error('Executable is required');
      }
      
      // If no specific agents provided, get all online agents
      let targetAgents = agent_ids;
      if (!targetAgents || targetAgents.length === 0) {
        const agentsResponse = await apiService.get('/api/agents');
        targetAgents = agentsResponse.agents
          .filter(a => a.status === 'online')
          .map(a => a.agent_id);
      }
      
      if (targetAgents.length === 0) {
        throw new Error('No online agents available');
      }
      
      const response = await apiService.post('/api/app-blocking/unblock', {
        agent_ids: targetAgents,
        executable: executable
      });
      
      return {
        success: true,
        message: `Application unblocking initiated on ${response.count} agent(s)`,
        command_ids: response.command_ids,
        count: response.count,
        online_agents: response.online_agents,
        offline_agents: response.offline_agents
      };
    } catch (error) {
      console.error('Failed to unblock application:', error);
      throw error;
    }
  }

  /**
   * Unblock all applications on specific agents
   */
  async unblockAllApplications(agentIds) {
    try {
      // If no specific agents provided, get all online agents
      let targetAgents = agentIds;
      if (!targetAgents || targetAgents.length === 0) {
        const agentsResponse = await apiService.get('/api/agents');
        targetAgents = agentsResponse.agents
          .filter(a => a.status === 'online')
          .map(a => a.agent_id);
      }
      
      if (targetAgents.length === 0) {
        throw new Error('No online agents available');
      }
      
      const response = await apiService.post('/api/app-blocking/unblock-all', {
        agent_ids: targetAgents
      });
      
      return {
        success: true,
        message: `Unblock all command sent to ${response.count} agent(s)`,
        command_ids: response.command_ids,
        count: response.count,
        online_agents: response.online_agents,
        offline_agents: response.offline_agents
      };
    } catch (error) {
      console.error('Failed to unblock all applications:', error);
      throw error;
    }
  }

  /**
   * Get installed applications from a specific agent
   */
  async getInstalledApplications(agentId) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }
      
      const response = await apiService.get(`/api/app-blocking/applications/${agentId}`);
      
      return {
        success: true,
        message: response.message,
        command_id: response.command_id,
        agent_hostname: response.agent_hostname
      };
    } catch (error) {
      console.error('Failed to get installed applications:', error);
      throw error;
    }
  }

  /**
   * Get blocked applications from a specific agent
   */
  async getBlockedApplications(agentId) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }
      
      const response = await apiService.get(`/api/app-blocking/blocked/${agentId}`);
      
      return {
        success: true,
        message: response.message,
        command_id: response.command_id,
        agent_hostname: response.agent_hostname
      };
    } catch (error) {
      console.error('Failed to get blocked applications:', error);
      throw error;
    }
  }

  /**
   * Verify if a specific application is blocked on an agent
   */
  async verifyAppBlocking(agentId, executable) {
    try {
      if (!agentId || !executable) {
        throw new Error('Agent ID and executable are required');
      }
      
      const response = await apiService.post('/api/app-blocking/verify', {
        agent_id: agentId,
        executable: executable
      });
      
      return {
        success: true,
        message: response.message,
        command_id: response.command_id,
        agent_hostname: response.agent_hostname,
        executable: executable
      };
    } catch (error) {
      console.error('Failed to verify app blocking:', error);
      throw error;
    }
  }

  /**
   * Verify if app is blocked (with polling for results)
   */
  async verifyAppBlockingWithResults(agentId, executable) {
    try {
      // Queue the verification command
      const queueResult = await this.verifyAppBlocking(agentId, executable);
      const commandId = queueResult.command_id;
      
      // Poll for results
      const result = await this.pollCommandUntilComplete(commandId);
      
      return {
        success: true,
        executable: executable,
        is_blocked: result.result.is_blocked || false,
        details: result.result.details || null,
        agent_hostname: queueResult.agent_hostname
      };
    } catch (error) {
      console.error('Failed to verify app blocking with results:', error);
      throw error;
    }
  }

  /**
   * Get currently blocked applications across all agents (actual state)
   */
  async getCurrentlyBlockedApps() {
    try {
      const agentsResponse = await apiService.get('/api/agents');
      const onlineAgents = agentsResponse.agents.filter(a => a.status === 'online');
      
      const blockedApps = [];
      const commandIds = [];
      
      // Queue commands to get blocked apps from each agent
      for (const agent of onlineAgents) {
        const result = await this.getBlockedApplications(agent.agent_id);
        commandIds.push({
          command_id: result.command_id,
          agent_id: agent.agent_id,
          hostname: agent.hostname
        });
      }
      
      // Wait and poll for all results
      for (const cmd of commandIds) {
        try {
          const result = await this.pollCommandUntilComplete(cmd.command_id, 20, 3000);
          
          if (result.success && result.result.blocked_applications) {
            result.result.blocked_applications.forEach(app => {
              blockedApps.push({
                ...app,
                agent_id: cmd.agent_id,
                hostname: cmd.hostname
              });
            });
          }
        } catch (error) {
          console.warn(`Failed to get blocked apps from ${cmd.hostname}`);
        }
      }
      
      return {
        success: true,
        blocked_applications: blockedApps,
        count: blockedApps.length
      };
    } catch (error) {
      console.error('Failed to get currently blocked apps:', error);
      throw error;
    }
  }

  /**
   * Get command status to retrieve application list results
   */
  async getCommandResult(commandId) {
    try {
      if (!commandId) {
        throw new Error('Command ID is required');
      }
      
      const response = await apiService.get(`/api/commands/status/${commandId}`);
      
      if (!response.success) {
        throw new Error(response.error || 'Command not found');
      }
      
      return {
        success: true,
        command: response.command
      };
    } catch (error) {
      console.error('Failed to get command result:', error);
      throw error;
    }
  }

  /**
   * Poll for command completion (for getting application lists)
   * Use this to wait for get_applications or get_blocked_applications results
   */
  async pollCommandUntilComplete(commandId, maxAttempts = 30, intervalMs = 2000) {
    for (let attempt = 0; attempt < maxAttempts; attempt++) {
      try {
        const result = await this.getCommandResult(commandId);
        
        if (result.command.status === 'completed') {
          return {
            success: true,
            result: result.command.result
          };
        } else if (result.command.status === 'failed') {
          throw new Error('Command failed: ' + (result.command.result?.error || 'Unknown error'));
        }
        
        // Wait before next poll
        await new Promise(resolve => setTimeout(resolve, intervalMs));
      } catch (error) {
        if (attempt === maxAttempts - 1) {
          throw error;
        }
      }
    }
    
    throw new Error('Command timed out waiting for completion');
  }

  /**
   * Get installed applications (with polling for results)
   */
  async getInstalledApplicationsWithResults(agentId) {
    try {
      // Queue the command
      const queueResult = await this.getInstalledApplications(agentId);
      const commandId = queueResult.command_id;
      
      // Poll for results
      const result = await this.pollCommandUntilComplete(commandId);
      
      return {
        success: true,
        applications: result.result.applications || [],
        count: result.result.count || 0,
        agent_hostname: queueResult.agent_hostname
      };
    } catch (error) {
      console.error('Failed to get installed applications with results:', error);
      throw error;
    }
  }

  /**
   * Get blocked applications (with polling for results)
   */
  async getBlockedApplicationsWithResults(agentId) {
    try {
      // Queue the command
      const queueResult = await this.getBlockedApplications(agentId);
      const commandId = queueResult.command_id;
      
      // Poll for results
      const result = await this.pollCommandUntilComplete(commandId);
      
      return {
        success: true,
        blocked_applications: result.result.blocked_applications || [],
        count: result.result.count || 0,
        agent_hostname: queueResult.agent_hostname
      };
    } catch (error) {
      console.error('Failed to get blocked applications with results:', error);
      throw error;
    }
  }

  /**
   * Get application blocking history for an agent
   */
  async getBlockingHistory(agentId) {
    try {
      const response = await apiService.get(`/api/commands/history/${agentId}`);
      
      const blockCommands = response.commands.filter(
        cmd => cmd.type === 'block_application' || cmd.type === 'unblock_application'
      );
      
      // Parse and structure the history
      const history = blockCommands.map(cmd => {
        let application = null;
        
        if (cmd.result) {
          const resultData = cmd.result.data || cmd.result;
          application = {
            name: resultData.name || 'Unknown',
            executable: resultData.executable || 'Unknown',
            methods_used: resultData.methods_used
          };
        }
        
        return {
          command_id: cmd.command_id,
          type: cmd.type,
          status: cmd.status,
          created_at: cmd.created_at,
          executed_at: cmd.executed_at,
          application: application,
          result: cmd.result
        };
      });
      
      return {
        success: true,
        history: history
      };
    } catch (error) {
      console.error('Failed to get blocking history:', error);
      throw error;
    }
  }

  /**
   * Get application blocking statistics across all agents
   */
  async getBlockingStats() {
    try {
      const agentsResponse = await apiService.get('/api/agents');
      const stats = {
        total_blocks: 0,
        total_unblocks: 0,
        agents_with_blocks: 0,
        recently_blocked: [],
        recently_unblocked: []
      };
      
      for (const agent of agentsResponse.agents) {
        try {
          const history = await apiService.get(`/api/commands/history/${agent.agent_id}`);
          
          const blockCommands = history.commands.filter(
            cmd => cmd.type === 'block_application' && cmd.status === 'completed'
          );
          
          const unblockCommands = history.commands.filter(
            cmd => cmd.type === 'unblock_application' && cmd.status === 'completed'
          );
          
          if (blockCommands.length > 0) {
            stats.agents_with_blocks++;
            stats.total_blocks += blockCommands.length;
            
            // Get recent blocks (last 10)
            blockCommands.slice(0, 10).forEach(cmd => {
              if (cmd.result && stats.recently_blocked.length < 10) {
                const resultData = cmd.result.data || cmd.result;
                stats.recently_blocked.push({
                  application: resultData.name || 'Unknown',
                  executable: resultData.executable || 'Unknown',
                  agent_id: agent.agent_id,
                  hostname: agent.hostname,
                  blocked_at: cmd.executed_at,
                  methods_used: resultData.methods_used
                });
              }
            });
          }
          
          if (unblockCommands.length > 0) {
            stats.total_unblocks += unblockCommands.length;
            
            // Get recent unblocks (last 10)
            unblockCommands.slice(0, 10).forEach(cmd => {
              if (cmd.result && stats.recently_unblocked.length < 10) {
                const resultData = cmd.result.data || cmd.result;
                stats.recently_unblocked.push({
                  executable: resultData.executable || 'Unknown',
                  agent_id: agent.agent_id,
                  hostname: agent.hostname,
                  unblocked_at: cmd.executed_at
                });
              }
            });
          }
        } catch (err) {
          console.warn(`Failed to get blocking stats for agent ${agent.agent_id}`);
        }
      }
      
      return {
        success: true,
        stats: stats
      };
    } catch (error) {
      console.error('Failed to get application blocking stats:', error);
      throw error;
    }
  }
}

const appBlockingService = new AppBlockingService();
export default appBlockingService;