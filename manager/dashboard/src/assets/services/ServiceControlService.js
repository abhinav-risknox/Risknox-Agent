// ============================================
// frontend/src/services/serviceControlService.js
// ============================================
import apiService from './apiService';

class ServiceControlService {
  constructor() {
    // Optimized timeouts for service operations
    this.pollInterval = 2000; // 2 seconds
    this.maxPollAttempts = 60; // 2 minutes total for regular commands
    this.serviceControlMaxAttempts = 120; // 4 minutes for service control operations
    
    console.log('ServiceControlService initialized:', {
      pollInterval: `${this.pollInterval}ms`,
      regularTimeout: `${Math.round((this.maxPollAttempts * this.pollInterval) / 1000)}s`,
      serviceControlTimeout: `${Math.round((this.serviceControlMaxAttempts * this.pollInterval) / 1000)}s`
    });
  }

  /**
   * Get service status for a specific agent
   * @param {string} agentId - Agent ID
   * @returns {Promise<Object>} Service status
   */
  async getServiceStatus(agentId) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }

      const response = await this.sendServiceCommand(agentId, 'status');
      
      if (!response.success) {
        throw new Error(response.error || 'Failed to get service status');
      }

      // Poll for result with extended timeout
      const result = await this.pollCommandResult(response.commandId, this.serviceControlMaxAttempts);
      
      return {
        success: true,
        data: result.data,
        agentId
      };
    } catch (error) {
      console.error('Get service status error:', error);
      throw error;
    }
  }

  /**
   * Start service on agent
   * @param {string} agentId - Agent ID
   * @param {string} password - Service password
   * @returns {Promise<Object>} Start result
   */
  async startService(agentId, password) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }
      
      if (!password) {
        throw new Error('Password is required to start the service');
      }

      const response = await this.sendServiceCommand(agentId, 'start', password);
      
      if (!response.success) {
        throw new Error(response.error || 'Failed to start service');
      }

      const result = await this.pollCommandResult(response.commandId, this.serviceControlMaxAttempts);
      
      return {
        success: true,
        action: 'start',
        data: result.data,
        message: 'Service started successfully',
        agentId
      };
    } catch (error) {
      console.error('Start service error:', error);
      throw error;
    }
  }

  /**
   * Stop service on agent
   * @param {string} agentId - Agent ID
   * @param {string} password - Service password
   * @returns {Promise<Object>} Stop result
   */
  async stopService(agentId, password) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }
      
      if (!password) {
        throw new Error('Password is required to stop the service');
      }

      const response = await this.sendServiceCommand(agentId, 'stop', password);
      
      if (!response.success) {
        throw new Error(response.error || 'Failed to stop service');
      }

      const result = await this.pollCommandResult(response.commandId, this.serviceControlMaxAttempts);
      
      return {
        success: true,
        action: 'stop',
        data: result.data,
        message: 'Service stopped successfully',
        agentId
      };
    } catch (error) {
      console.error('Stop service error:', error);
      throw error;
    }
  }

  /**
   * Restart service on agent
   * @param {string} agentId - Agent ID
   * @param {string} password - Service password
   * @returns {Promise<Object>} Restart result
   */
  async restartService(agentId, password) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }
      
      if (!password) {
        throw new Error('Password is required to restart the service');
      }

      const response = await this.sendServiceCommand(agentId, 'restart', password);
      
      if (!response.success) {
        throw new Error(response.error || 'Failed to restart service');
      }

      const result = await this.pollCommandResult(response.commandId, this.serviceControlMaxAttempts);
      
      return {
        success: true,
        action: 'restart',
        data: result.data,
        message: 'Service restarted successfully',
        agentId
      };
    } catch (error) {
      console.error('Restart service error:', error);
      throw error;
    }
  }

  /**
   * Execute service control with progress callback
   * @param {string} agentId - Agent ID
   * @param {string} action - Action (start, stop, restart, status)
   * @param {string} password - Service password (required for start/stop/restart)
   * @param {Function} onProgress - Progress callback
   * @returns {Promise<Object>} Execution result
   */
  async executeServiceControl(agentId, action, password = null, onProgress = null) {
    try {
      if (!agentId) {
        throw new Error('Agent ID is required');
      }

      const validActions = ['start', 'stop', 'restart', 'status'];
      if (!validActions.includes(action)) {
        throw new Error(`Invalid action. Must be one of: ${validActions.join(', ')}`);
      }
      
      // Check if password is required
      const protectedActions = ['start', 'stop', 'restart'];
      if (protectedActions.includes(action) && !password) {
        throw new Error(`Password is required for ${action} action`);
      }

      // Send command with password
      const cmdResponse = await this.sendServiceCommand(agentId, action, password);
      
      if (!cmdResponse.success) {
        throw new Error(cmdResponse.error || 'Failed to send command');
      }

      // Use extended timeout for service control operations
      const maxAttempts = this.serviceControlMaxAttempts;
      const totalTimeoutSeconds = Math.round((maxAttempts * this.pollInterval) / 1000);
      
      console.log(`Polling for service ${action} command (timeout: ${totalTimeoutSeconds}s)...`);

      // Poll for result with progress updates
      let attempts = 0;
      let consecutiveErrors = 0;
      const maxConsecutiveErrors = 5; // Allow 5 consecutive errors before giving up

      while (attempts < maxAttempts) {
        await this.sleep(this.pollInterval);
        attempts++;
        
        try {
          const command = await apiService.get(`/api/commands/status/${cmdResponse.commandId}`);
          
          // Reset consecutive error counter on successful API call
          consecutiveErrors = 0;
          
          // Progress callback
          if (onProgress) {
            const elapsedSeconds = Math.round((attempts * this.pollInterval) / 1000);
            onProgress({
              status: command.command?.status || 'pending',
              attempts,
              maxAttempts: maxAttempts,
              progress: Math.round((attempts / maxAttempts) * 100),
              elapsedTime: elapsedSeconds,
              totalTimeout: totalTimeoutSeconds
            });
          }
          
          if (command.command?.status === 'completed') {
            const executionTime = Math.round((attempts * this.pollInterval) / 1000);
            console.log(`Service ${action} completed after ${executionTime}s`);
            return {
              success: true,
              data: command.command.result,
              action,
              agentId,
              executionTime
            };
          }
          
          if (command.command?.status === 'failed') {
            const errorMsg = command.command.result?.error || 
                           command.command.result?.data?.error || 
                           'Command failed';
            throw new Error(errorMsg);
          }
        } catch (error) {
          // If it's a command failure (not a network error), rethrow immediately
          if (error.message.includes('Command failed') || 
              error.message.includes('password') ||
              error.message.includes('Password') ||
              error.message.includes('Authentication')) {
            throw error;
          }
          
          // Track consecutive network/API errors
          consecutiveErrors++;
          console.warn(
            `Polling attempt ${attempts} failed (${consecutiveErrors}/${maxConsecutiveErrors}):`, 
            error.message
          );
          
          // If too many consecutive errors, assume agent is offline
          if (consecutiveErrors >= maxConsecutiveErrors) {
            throw new Error(
              `Agent appears to be offline or unreachable (${consecutiveErrors} consecutive polling failures)`
            );
          }
          
          // Otherwise continue polling (transient network issues)
        }
      }
      
      // Timeout reached
      throw new Error(
        `Service ${action} operation timed out after ${totalTimeoutSeconds} seconds. ` +
        `The command may still be executing on the agent. Check the agent logs or try the status command.`
      );
    } catch (error) {
      console.error('Execute service control error:', error);
      throw error;
    }
  }

  /**
   * Send service command to agent
   * @param {string} agentId - Agent ID
   * @param {string} action - Action to perform
   * @param {string} password - Password for protected actions (optional)
   * @returns {Promise<Object>} Command response
   */
  async sendServiceCommand(agentId, action, password = null) {
    try {
      const payload = { action };
      
      // Add password for actions that require it
      const protectedActions = ['start', 'stop', 'restart'];
      if (protectedActions.includes(action)) {
        if (!password) {
          throw new Error(`Password is required for ${action} action`);
        }
        payload.password = password;
      }
      
      const response = await apiService.post(`/api/agents/${agentId}/service/control`, payload);

      if (!response || !response.success) {
        throw new Error(response?.error || 'Failed to send command');
      }

      return {
        success: true,
        commandId: response.command_id,
        message: response.message
      };
    } catch (error) {
      console.error('Send service command error:', error);
      throw error;
    }
  }

  /**
   * Poll for command result
   * @param {string} commandId - Command ID
   * @param {number} maxAttempts - Maximum polling attempts (optional)
   * @returns {Promise<Object>} Command result
   */
  async pollCommandResult(commandId, maxAttempts = null) {
    const attempts_limit = maxAttempts || this.maxPollAttempts;
    let attempts = 0;
    let consecutiveErrors = 0;
    const maxConsecutiveErrors = 5;
    
    while (attempts < attempts_limit) {
      await this.sleep(this.pollInterval);
      attempts++;
      
      try {
        const response = await apiService.get(`/api/commands/status/${commandId}`);
        const command = response.command;
        
        // Reset error counter on successful request
        consecutiveErrors = 0;
        
        if (command.status === 'completed') {
          return {
            success: true,
            data: command.result,
            commandId
          };
        }
        
        if (command.status === 'failed') {
          throw new Error(command.result?.error || 'Command execution failed');
        }
      } catch (error) {
        if (error.message.includes('Command execution failed')) {
          throw error;
        }
        
        // Track consecutive errors
        consecutiveErrors++;
        if (consecutiveErrors >= maxConsecutiveErrors) {
          throw new Error('Agent appears to be offline or unreachable');
        }
        // Continue polling on transient network errors
      }
    }
    
    const timeoutSeconds = Math.round((attempts_limit * this.pollInterval) / 1000);
    throw new Error(
      `Command execution timeout after ${timeoutSeconds} seconds. Agent may be offline or busy.`
    );
  }

  /**
   * Get service control command history for an agent
   * @param {string} agentId - Agent ID
   * @param {number} limit - Number of commands to retrieve
   * @returns {Promise<Object>} Command history
   */
  async getCommandHistory(agentId, limit = 20) {
    try {
      const response = await apiService.get(`/api/commands/history/${agentId}`);
      
      const serviceCommands = response.commands.filter(
        cmd => cmd.type === 'service_control'
      ).slice(0, limit);
      
      return {
        success: true,
        history: serviceCommands,
        count: serviceCommands.length
      };
    } catch (error) {
      console.error('Failed to get command history:', error);
      throw error;
    }
  }

  /**
   * Get status of multiple service control commands
   * @param {Array<string>} commandIds - Array of command IDs
   * @returns {Promise<Object>} Status summary
   */
  async getCommandsStatus(commandIds) {
    try {
      if (!commandIds || commandIds.length === 0) {
        throw new Error('Command IDs are required');
      }

      const promises = commandIds.map(id => 
        apiService.get(`/api/commands/status/${id}`).catch(err => ({
          command_id: id,
          status: 'error',
          error: err.message
        }))
      );

      const results = await Promise.all(promises);
      
      const summary = {
        total: results.length,
        pending: results.filter(r => r.command?.status === 'pending').length,
        completed: results.filter(r => r.command?.status === 'completed').length,
        failed: results.filter(r => r.command?.status === 'failed').length,
        commands: results.map(r => r.command)
      };

      return {
        success: true,
        ...summary
      };
    } catch (error) {
      console.error('Failed to get commands status:', error);
      throw error;
    }
  }

  /**
   * Batch service control for multiple agents
   * @param {Array<string>} agentIds - Array of agent IDs
   * @param {string} action - Action to perform
   * @param {string} password - Service password (required for start/stop/restart)
   * @returns {Promise<Object>} Batch result
   */
  async batchServiceControl(agentIds, action, password = null) {
    try {
      if (!agentIds || agentIds.length === 0) {
        throw new Error('No agents selected');
      }

      const validActions = ['start', 'stop', 'restart', 'status'];
      if (!validActions.includes(action)) {
        throw new Error(`Invalid action: ${action}`);
      }
      
      // Check password requirement
      const protectedActions = ['start', 'stop', 'restart'];
      if (protectedActions.includes(action) && !password) {
        throw new Error(`Password is required for ${action} action`);
      }

      // Send commands to all agents
      const promises = agentIds.map(agentId =>
        this.sendServiceCommand(agentId, action, password).catch(err => ({
          agentId,
          success: false,
          error: err.message
        }))
      );

      const results = await Promise.all(promises);
      
      const successful = results.filter(r => r.success);
      const failed = results.filter(r => !r.success);

      return {
        success: true,
        total: agentIds.length,
        successful: successful.length,
        failed: failed.length,
        commandIds: successful.map(r => r.commandId),
        results
      };
    } catch (error) {
      console.error('Batch service control error:', error);
      throw error;
    }
  }

  /**
   * Cancel pending command
   * @param {string} commandId - Command ID to cancel
   * @returns {Promise<Object>} Cancellation result
   */
  async cancelCommand(commandId) {
    try {
      const response = await apiService.post(`/api/commands/${commandId}/cancel`);
      
      return {
        success: true,
        message: response.message || 'Command cancelled successfully'
      };
    } catch (error) {
      console.error('Cancel command error:', error);
      throw error;
    }
  }

  /**
   * Sleep utility
   * @param {number} ms - Milliseconds
   * @returns {Promise<void>}
   */
  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  /**
   * Validate action
   * @param {string} action - Action to validate
   * @returns {boolean} Is valid
   */
  isValidAction(action) {
    const validActions = ['start', 'stop', 'restart', 'status'];
    return validActions.includes(action);
  }
  
  /**
   * Check if action requires password
   * @param {string} action - Action to check
   * @returns {boolean} Requires password
   */
  requiresPassword(action) {
    const protectedActions = ['start', 'stop', 'restart'];
    return protectedActions.includes(action);
  }
}

const serviceControlService = new ServiceControlService();
export default serviceControlService;