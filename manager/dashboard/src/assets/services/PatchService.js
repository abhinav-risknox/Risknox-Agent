// frontend/src/services/patchService.js - COMPLETE VERSION WITH SCHEDULING
import apiService from './apiService';

class PatchService {
  /**
   * Get patch information from all agents
   */
  async getPatchInfo() {
    try {
      const response = await apiService.get('/api/patch-info');
      
      if (!response || !response.success) {
        throw new Error(response?.error || 'Failed to get patch info');
      }
      
      return {
        success: true,
        agents: response.agents || []
      };
    } catch (error) {
      console.error('Failed to get patch info:', error);
      throw error;
    }
  }

  /**
   * Request fresh patch information from specific agents
   */
  async requestPatchInfo(agentIds) {
    try {
      const response = await apiService.post('/api/commands/trigger', {
        agent_ids: agentIds,
        command_type: 'patch',
        parameters: {}
      });
      
      return {
        success: true,
        message: 'Patch info request sent to agents',
        command_ids: response.command_ids,
        count: response.count
      };
    } catch (error) {
      console.error('Failed to request patch info:', error);
      throw error;
    }
  }

  /**
   * Install patches on selected agents (immediate or scheduled)
   * @param {Array} agentIds - Array of agent IDs
   * @param {Array} patchIds - Array of patch IDs to install
   * @param {boolean} installAll - Install all available patches
   * @param {string} scheduledTime - Optional ISO datetime string for scheduled installation
   */
  async installPatches(agentIds, patchIds = [], installAll = false, scheduledTime = null) {
    try {
      if (!agentIds || agentIds.length === 0) {
        throw new Error('No agents selected');
      }

      console.log('Installing patches:', { agentIds, patchIds, installAll, scheduledTime });

      const payload = {
        agent_ids: agentIds,
        patch_ids: patchIds,
        install_all: installAll
      };

      // Add scheduled_time if provided
      if (scheduledTime) {
        payload.scheduled_time = scheduledTime;
      }

      const response = await apiService.post('/api/patch-management/install', payload);

      // Handle scheduled installation response
      if (scheduledTime) {
        return {
          success: true,
          scheduled: true,
          message: response.message || 'Patch installation scheduled',
          job_id: response.job_id,
          scheduled_time: response.scheduled_time,
          agent_count: response.agent_count,
          install_all: response.install_all
        };
      }

      // Handle immediate installation response
      return {
        success: true,
        scheduled: false,
        message: response.message || 'Patch installation initiated',
        command_ids: response.command_ids,
        count: response.count,
        online_agents: response.online_agents,
        offline_agents: response.offline_agents,
        warning: response.warning
      };
    } catch (error) {
      console.error('Failed to install patches:', error);
      throw error;
    }
  }

  /**
   * Get all scheduled patch jobs
   */
  async getScheduledJobs() {
    try {
      const response = await apiService.get('/api/patch-management/scheduled');
      
      if (!response || !response.success) {
        throw new Error(response?.error || 'Failed to get scheduled jobs');
      }
      
      return {
        success: true,
        scheduled_jobs: response.scheduled_jobs || [],
        count: response.count
      };
    } catch (error) {
      console.error('Failed to get scheduled jobs:', error);
      throw error;
    }
  }

  /**
   * Update a scheduled patch job
   * @param {string} jobId - Job ID to update
   * @param {Object} updates - Object containing fields to update
   */
  async updateScheduledJob(jobId, updates) {
    try {
      if (!jobId) {
        throw new Error('Job ID is required');
      }

      const response = await apiService.put(`/api/patch-management/scheduled/${jobId}`, updates);
      
      return {
        success: true,
        message: response.message || 'Scheduled job updated successfully'
      };
    } catch (error) {
      console.error('Failed to update scheduled job:', error);
      throw error;
    }
  }

  /**
   * Cancel a scheduled patch job
   * @param {string} jobId - Job ID to cancel
   */
  async cancelScheduledJob(jobId) {
    try {
      if (!jobId) {
        throw new Error('Job ID is required');
      }

      const response = await apiService.delete(`/api/patch-management/scheduled/${jobId}`);
      
      return {
        success: true,
        message: response.message || 'Scheduled job cancelled successfully'
      };
    } catch (error) {
      console.error('Failed to cancel scheduled job:', error);
      throw error;
    }
  }

  /**
   * Get status of a specific command
   */
  async getCommandStatus(commandId) {
    try {
      const response = await apiService.get(`/api/commands/status/${commandId}`);
      return response;
    } catch (error) {
      console.error('Failed to get command status:', error);
      throw error;
    }
  }

  /**
   * Get status of multiple installation commands
   */
  async getInstallationStatus(commandIds) {
    try {
      const response = await apiService.post('/api/patch-management/install/status', {
        command_ids: commandIds
      });
      
      return {
        success: true,
        statuses: response.statuses,
        total: response.total,
        pending: response.pending,
        completed: response.completed,
        failed: response.failed
      };
    } catch (error) {
      console.error('Failed to get installation status:', error);
      throw error;
    }
  }

  /**
   * Get patch history for a specific agent
   */
  async getPatchHistory(agentId) {
    try {
      const response = await apiService.get(`/api/commands/history/${agentId}`);
      
      const patchCommands = response.commands.filter(
        cmd => cmd.type === 'patch' || cmd.type === 'install_patches'
      );
      
      return {
        success: true,
        history: patchCommands
      };
    } catch (error) {
      console.error('Failed to get patch history:', error);
      throw error;
    }
  }

  /**
   * Poll installation status until completion
   */
  async pollInstallationStatus(commandIds, onProgress, maxAttempts = 60) {
    let attempts = 0;
    
    const poll = async () => {
      attempts++;
      
      try {
        const status = await this.getInstallationStatus(commandIds);
        
        if (onProgress) {
          onProgress(status);
        }
        
        // Check if all completed or failed
        const allDone = status.pending === 0;
        
        if (allDone || attempts >= maxAttempts) {
          return status;
        }
        
        // Wait 5 seconds before next poll
        await new Promise(resolve => setTimeout(resolve, 5000));
        return poll();
        
      } catch (error) {
        console.error('Error polling status:', error);
        if (attempts < maxAttempts) {
          await new Promise(resolve => setTimeout(resolve, 5000));
          return poll();
        }
        throw error;
      }
    };
    
    return poll();
  }
}

const patchService = new PatchService();
export default patchService;