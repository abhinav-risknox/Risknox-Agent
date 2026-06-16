// ============================================
// frontend/src/services/webBlockingService.js
// ============================================
import apiService from './apiService';

class WebBlockingService {
  /**
   * Get all web filtering policies
   */
  async getBlockedUrls() {
    try {
      const response = await apiService.get('/api/policies');
      
      return {
        success: true,
        policies: response.policies,
        count: response.count
      };
    } catch (error) {
      console.error('Failed to get blocked URLs:', error);
      throw error;
    }
  }

  /**
   * Block URLs on specific agents
   */
  async blockUrl(urlData) {
    try {
      const { url, agent_ids } = urlData;
      
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
      
      const response = await apiService.post('/api/commands/trigger', {
        agent_ids: targetAgents,
        command_type: 'web_blocking',
        parameters: {
          urls: Array.isArray(url) ? url : [url]
        }
      });
      
      return {
        success: true,
        message: `URL blocking initiated on ${response.count} agents`,
        command_ids: response.command_ids,
        count: response.count
      };
    } catch (error) {
      console.error('Failed to block URL:', error);
      throw error;
    }
  }

  /**
   * Unblock URLs on specific agents
   */
  async unblockUrl(urlData) {
    try {
      const { url, agent_ids } = urlData;
      
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
      
      const response = await apiService.post('/api/unblock-url', {
        url: url,  
        agent_ids: targetAgents
      });
      
      return {
        success: true,
        message: `URL unblocking initiated on ${response.count} agents`,
        command_ids: response.command_ids
      };
    } catch (error) {
      console.error('Failed to unblock URL:', error);
      throw error;
    }
  }

  /**
   * Get blocked URLs from agent - queries the agent's current blocked list
   */
  async getAgentBlockedUrls(agentId) {
    try {
      // Send command to get current blocked URLs from agent
      const response = await apiService.get(`/api/web-blocking/blocked/${agentId}`);
      
      if (!response.success) {
        console.warn(`Failed to queue get_blocked_urls for agent ${agentId}:`, response.error);
        return {
          success: false,
          blocked_urls: [],
          error: response.error
        };
      }

      const commandId = response.command_id;
      console.log(`Polling for blocked URLs from agent ${agentId}, command: ${commandId}`);
      
      // Poll for result
      let attempts = 0;
      const maxAttempts = 10;
      
      while (attempts < maxAttempts) {
        await new Promise(resolve => setTimeout(resolve, 1000));
        
        const statusResponse = await apiService.get(`/api/commands/status/${commandId}`);
        
        if (statusResponse.success && statusResponse.command.status === 'completed') {
          const result = statusResponse.command.result;
          console.log(`Got blocked URLs from agent ${agentId}:`, result);
          
          // Handle different result formats - agent returns 'urls' not 'blocked_urls'
          let blockedUrls = [];
          
          // Try 'urls' first (what agent actually returns)
          if (result.urls && Array.isArray(result.urls)) {
            blockedUrls = result.urls.map(item => {
              if (typeof item === 'string') {
                return { url: item, blocked_at: new Date().toISOString() };
              }
              // If it's already an object, make sure it has required fields
              return {
                url: item.url || item,
                blocked_at: item.blocked_at || new Date().toISOString()
              };
            });
          } 
          // Fallback to 'blocked_urls' for compatibility
          else if (result.blocked_urls && Array.isArray(result.blocked_urls)) {
            blockedUrls = result.blocked_urls.map(item => {
              if (typeof item === 'string') {
                return { url: item, blocked_at: new Date().toISOString() };
              }
              return {
                url: item.url || item,
                blocked_at: item.blocked_at || new Date().toISOString()
              };
            });
          }
          
          console.log(`Processed ${blockedUrls.length} blocked URLs for agent ${agentId}`);
          
          return {
            success: true,
            blocked_urls: blockedUrls
          };
        }
        
        if (statusResponse.success && statusResponse.command.status === 'failed') {
          console.error(`Command failed for agent ${agentId}`);
          return {
            success: false,
            blocked_urls: [],
            error: 'Command failed on agent'
          };
        }
        
        attempts++;
      }
      
      // Timeout
      console.warn(`Timeout getting blocked URLs for agent ${agentId} after ${maxAttempts} attempts`);
      return {
        success: false,
        blocked_urls: [],
        error: 'Timeout waiting for agent response'
      };
    } catch (error) {
      console.error('Failed to get agent blocked URLs:', error);
      return {
        success: false,
        blocked_urls: [],
        error: error.message
      };
    }
  }

  /**
   * Verify if a URL is blocked on a specific agent
   * NEW: Uses the /api/web-blocking/verify endpoint
   */
  async verifyUrlBlocked(agentId, url) {
    try {
      const response = await apiService.post('/api/web-blocking/verify', {
        agent_id: agentId,
        url: url
      });

      if (!response.success) {
        throw new Error(response.error || 'Failed to send verification command');
      }

      return {
        success: true,
        command_id: response.command_id,
        message: response.message
      };
    } catch (error) {
      console.error('Failed to verify URL blocking:', error);
      throw error;
    }
  }

  /**
   * Poll for verification result
   * NEW: Call this after verifyUrlBlocked to get the actual result
   */
  async getVerificationResult(commandId, maxAttempts = 10) {
    try {
      let attempts = 0;
      
      while (attempts < maxAttempts) {
        await new Promise(resolve => setTimeout(resolve, 1000));
        
        const statusResponse = await apiService.get(`/api/commands/status/${commandId}`);
        
        if (statusResponse.success && statusResponse.command.status === 'completed') {
          const result = statusResponse.command.result;
          
          return {
            success: true,
            is_blocked: result.is_blocked || false,
            details: result.details || null,
            verified_at: statusResponse.command.executed_at
          };
        }
        
        if (statusResponse.success && statusResponse.command.status === 'failed') {
          throw new Error('Verification command failed on agent');
        }
        
        attempts++;
      }
      
      // Timeout
      return {
        success: false,
        error: 'Verification timeout',
        is_blocked: null
      };
    } catch (error) {
      console.error('Failed to get verification result:', error);
      throw error;
    }
  }

  /**
   * Complete verification flow - sends command and waits for result
   * NEW: Convenience method that combines verifyUrlBlocked + getVerificationResult
   */
  async verifyAndGetResult(agentId, url) {
    try {
      const verifyResponse = await this.verifyUrlBlocked(agentId, url);
      
      if (!verifyResponse.success) {
        throw new Error('Failed to initiate verification');
      }

      const result = await this.getVerificationResult(verifyResponse.command_id);
      
      return result;
    } catch (error) {
      console.error('Verification flow failed:', error);
      throw error;
    }
  }

  /**
   * Get web blocking statistics
   */
  async getWebBlockingStats() {
    try {
      const agentsResponse = await apiService.get('/api/agents');
      const onlineAgents = agentsResponse.agents.filter(a => a.status === 'online');
      
      const stats = {
        total_blocks: 0,
        agents_with_blocks: 0,
        recently_blocked: []
      };
      
      console.log(`Getting blocking stats from ${onlineAgents.length} online agents...`);
      
      for (const agent of onlineAgents) {
        try {
          const response = await this.getAgentBlockedUrls(agent.agent_id);
          
          if (response.success && response.blocked_urls && response.blocked_urls.length > 0) {
            stats.agents_with_blocks++;
            stats.total_blocks += response.blocked_urls.length;
            
            // Add to recently blocked list
            response.blocked_urls.forEach(block => {
              stats.recently_blocked.push({
                url: block.url,
                agent_id: agent.agent_id,
                hostname: agent.hostname,
                blocked_at: block.blocked_at || new Date().toISOString()
              });
            });
            
            console.log(`Agent ${agent.hostname} has ${response.blocked_urls.length} blocked URLs`);
          } else {
            console.log(`Agent ${agent.hostname} has no blocked URLs or query failed`);
          }
        } catch (err) {
          console.warn(`Failed to get blocking stats for agent ${agent.agent_id}:`, err);
        }
      }
      
      // Sort recently blocked by date (most recent first) and limit to 10
      stats.recently_blocked.sort((a, b) => 
        new Date(b.blocked_at) - new Date(a.blocked_at)
      );
      stats.recently_blocked = stats.recently_blocked.slice(0, 10);
      
      console.log(`Total blocking stats: ${stats.total_blocks} blocks across ${stats.agents_with_blocks} agents`);
      
      return {
        success: true,
        stats: stats
      };
    } catch (error) {
      console.error('Failed to get web blocking stats:', error);
      throw error;
    }
  }

  /**
   * Bulk verify URLs across multiple agents
   * NEW: Verify multiple URLs at once
   */
  async bulkVerifyUrls(verificationList) {
    try {
      const results = [];
      
      for (const item of verificationList) {
        try {
          const result = await this.verifyAndGetResult(item.agent_id, item.url);
          results.push({
            agent_id: item.agent_id,
            url: item.url,
            ...result
          });
        } catch (err) {
          results.push({
            agent_id: item.agent_id,
            url: item.url,
            success: false,
            error: err.message
          });
        }
      }
      
      return {
        success: true,
        results: results,
        total: verificationList.length,
        verified: results.filter(r => r.success).length,
        failed: results.filter(r => !r.success).length
      };
    } catch (error) {
      console.error('Bulk verification failed:', error);
      throw error;
    }
  }
}

const webBlockingService = new WebBlockingService();
export default webBlockingService;