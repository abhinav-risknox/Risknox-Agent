// ============================================
// frontend/src/services/antivirusService.js
// ============================================
import apiService from './apiService';

class AntivirusService {
  // ============================================
  // SCAN OPERATIONS
  // ============================================

  /**
   * Trigger antivirus scan command on agents
   */
  async startScan(scanData) {
    try {
      console.log('[AntivirusService] startScan called with:', scanData);
      
      const response = await apiService.post('/api/commands/trigger', {
        agent_ids: [scanData.agent_id],
        command_type: 'scan',
        parameters: {
          path: scanData.path,
          scan_type: scanData.scan_type
        }
      });

      if (!response.command_ids || response.command_ids.length === 0) {
        throw new Error('No command ID returned from server');
      }

      const commandId = response.command_ids[0];
      
      console.log('[AntivirusService] Scan command created:', commandId);
      
      return {
        success: true,
        session_id: commandId, 
        command_id: commandId
      };
    } catch (error) {
      console.error('[AntivirusService] startScan failed:', error);
      throw error;
    }
  }

  /**
   * Get scan status using command_id
   */
  async getScanStatus(commandId) {
    try {
      console.log('[AntivirusService] getScanStatus called with commandId:', commandId);
      
      const response = await apiService.get(`/api/commands/status/${commandId}`);
      
      if (!response.success) {
        throw new Error('Failed to get scan status');
      }

      console.log('[AntivirusService] Status response:', response.command);
      
      const commandData = response.command;
      const result = commandData.result || {};
      
      return {
        success: true,
        session: result 
      };
    } catch (error) {
      console.error('[AntivirusService] getScanStatus failed:', error);
      throw error;
    }
  }

  /**
   * Get detailed scan progress
   */
  async getScanProgress(commandId) {
    try {
      console.log('[AntivirusService] getScanProgress called with commandId:', commandId);
      
      const response = await apiService.get(`/api/commands/status/${commandId}`);
      
      if (!response.success) {
        throw new Error('Failed to get scan progress');
      }
      
      const command = response.command;
      const result = command.result || {};
      
      const progress = {
        progress_percent: result.progress_percent || 0,
        files_scanned: result.files_scanned || 0,
        total_files: result.total_files || 0,
        threats_found: result.threats_found || 0,
        status: result.status || command.status,
        current_file: result.current_file || '',
        scan_log: result.scan_log || []
      };
      
      console.log('[AntivirusService] Scan progress:', progress);
      
      return {
        success: true,
        command_id: command.command_id,
        status: command.status,
        progress: progress
      };
      
    } catch (error) {
      console.error('[AntivirusService] getScanProgress failed:', error);
      throw error;
    }
  }

  /**
   * Cancel an active scan
   */
  async cancelScan(agentId, sessionId) {
    try {
      console.log('[AntivirusService] cancelScan called with:', { agentId, sessionId });
      
      const response = await apiService.post('/api/antivirus/cancel', {
        agent_id: agentId,
        session_id: sessionId
      });
      
      console.log('[AntivirusService] Cancel scan response:', response);
      
      return {
        success: true,
        command_id: response.command_id,
        message: response.message
      };
    } catch (error) {
      console.error('[AntivirusService] cancelScan failed:', error);
      throw error;
    }
  }

  // ============================================
  // ACTIVE SCAN MONITORING (NEW)
  // ============================================

  /**
   * Get all active scans across all agents
   */
  async getActiveScans() {
    try {
      console.log('[AntivirusService] getActiveScans called');
      
      const agentsResponse = await apiService.get('/api/agents');
      const activeScans = [];
      
      for (const agent of agentsResponse.agents) {
        try {
          // Get recent command history (last 50 commands)
          const history = await apiService.get(`/api/commands/history/${agent.agent_id}`);
          
          // Find the most recent scan command
          const recentScanCommand = history.commands.find(cmd => cmd.type === 'scan');
          
          if (recentScanCommand && recentScanCommand.result) {
            const result = recentScanCommand.result;
            
            // Check if this is a scan start result (has session_id and status "started")
            if (result.type === 'scan' && result.session_id && result.status === 'started') {
              // Now check if there's a completion status for this session
              const statusCommands = history.commands.filter(cmd => 
                cmd.type === 'scan' && 
                cmd.result &&
                cmd.result.session_id === result.session_id &&
                cmd.result.type === 'get_scan_status'
              );
              
              // If no completion status found, or latest status is not completed, it's active
              const latestStatus = statusCommands.length > 0 
                ? statusCommands[0].result 
                : null;
              
              const isActive = !latestStatus || 
                             !['completed', 'error', 'cancelled'].includes(latestStatus.status);
              
              if (isActive) {
                activeScans.push({
                  agent_id: agent.agent_id,
                  hostname: agent.hostname,
                  command_id: recentScanCommand.command_id,
                  session_id: result.session_id,
                  scan_type: result.scan_type,
                  path: result.path,
                  started_at: recentScanCommand.created_at,
                  status: latestStatus ? latestStatus.status : 'scanning',
                  progress: latestStatus ? {
                    progress_percent: latestStatus.progress_percent || 0,
                    files_scanned: latestStatus.files_scanned || 0,
                    threats_found: latestStatus.threats_found || 0
                  } : null
                });
              }
            }
          }
        } catch (err) {
          console.warn(`[AntivirusService] Failed to check agent ${agent.agent_id}:`, err.message);
        }
      }
      
      console.log('[AntivirusService] Active scans found:', activeScans.length);
      return {
        success: true,
        active_scans: activeScans
      };
    } catch (error) {
      console.error('[AntivirusService] getActiveScans failed:', error);
      throw error;
    }
  }

  /**
   * Request scan progress from a specific agent
   */
  async requestScanProgress(agentId, sessionId) {
    try {
      console.log('[AntivirusService] requestScanProgress called:', { agentId, sessionId });
      
      const response = await apiService.post('/api/antivirus/scan-progress', {
        agent_id: agentId,
        session_id: sessionId
      });
      
      return {
        success: true,
        command_id: response.command_id,
        agent_hostname: response.agent_hostname,
        session_id: response.session_id
      };
    } catch (error) {
      console.error('[AntivirusService] requestScanProgress failed:', error);
      throw error;
    }
  }

  /**
   * Get scan progress result from command status
   */
  async getScanProgressResult(commandId) {
    try {
      const response = await apiService.get(`/api/commands/status/${commandId}`);
      
      if (!response.success || !response.command) {
        throw new Error('Failed to get command status');
      }
      
      const command = response.command;
      const result = command.result || {};
      
      // Extract progress data from the result
      const progress = result.progress || {};
      
      return {
        success: true,
        status: command.status,
        progress: {
          status: progress.status || 'unknown',
          progress_percent: progress.progress_percent || 0,
          files_scanned: progress.files_scanned || 0,
          total_files: progress.total_files || 0,
          threats_found: progress.threats_found || 0,
          current_file: progress.current_file || '',
          scan_log: progress.scan_log || []
        }
      };
    } catch (error) {
      console.error('[AntivirusService] getScanProgressResult failed:', error);
      throw error;
    }
  }

  // ============================================
  // METRICS & THREATS
  // ============================================

  /**
   * Get antivirus metrics from all agents
   */
  async getAntivirusMetrics() {
    try {
      console.log('[AntivirusService] getAntivirusMetrics called');
      
      const response = await apiService.get('/api/antivirus/metrics');
      
      if (!response.success) {
        throw new Error('Failed to get antivirus metrics');
      }
      
      console.log('[AntivirusService] Metrics from API:', response.metrics);
      
      return {
        success: true,
        metrics: response.metrics
      };
    } catch (error) {
      console.error('[AntivirusService] getAntivirusMetrics failed:', error);
      
      return {
        success: false,
        metrics: {
          total_scans: 0,
          active_scans: 0,
          completed_scans: 0,
          threats_found: 0,
          agents_with_threats: 0
        }
      };
    }
  }

  /**
   * Get all antivirus threats from agents
   */
  async getAntivirusThreats() {
    try {
      console.log('[AntivirusService] getAntivirusThreats called');
      
      const agentsResponse = await apiService.get('/api/agents');
      const threats = [];
      
      for (const agent of agentsResponse.agents) {
        try {
          const history = await apiService.get(`/api/commands/history/${agent.agent_id}`);
          
          const scanCommands = history.commands.filter(cmd => {
            if (cmd.type !== 'scan' || cmd.status !== 'completed') return false;
            
            const result = cmd.result || {};
            return result.type === 'get_scan_status' && 
                   result.threats && 
                   Array.isArray(result.threats) && 
                   result.threats.length > 0;
          });
          
          scanCommands.forEach(cmd => {
            cmd.result.threats.forEach(threat => {
              threats.push({
                agent_id: agent.agent_id,
                hostname: agent.hostname,
                command_id: cmd.command_id,
                detected_at: cmd.executed_at,
                file: threat.file,
                threat_name: threat.threat,
                timestamp: threat.timestamp
              });
            });
          });
        } catch (err) {
          console.warn(`[AntivirusService] Failed to get threats for agent ${agent.agent_id}`);
        }
      }
      
      console.log('[AntivirusService] Found threats:', threats.length);
      
      return {
        success: true,
        threats: threats
      };
    } catch (error) {
      console.error('[AntivirusService] getAntivirusThreats failed:', error);
      throw error;
    }
  }

  // ============================================
  // SCHEDULED SCANS
  // ============================================

  /**
   * Get scheduled scans
   */
  async getScheduledScans() {
    try {
      const response = await apiService.get('/api/antivirus/scheduled');
      return {
        success: true,
        scheduled_scans: response.scheduled_scans || []
      };
    } catch (error) {
      console.warn('[AntivirusService] getScheduledScans failed:', error.message);
      return { success: true, scheduled_scans: [] };
    }
  }

  /**
   * Create scheduled scan
   */
  async createScheduledScan(scheduleData) {
    try {
      const response = await apiService.post('/api/antivirus/scheduled', scheduleData);
      return {
        success: true,
        ...response
      };
    } catch (error) {
      console.error('[AntivirusService] createScheduledScan failed:', error);
      throw error;
    }
  }

  /**
   * Update scheduled scan
   */
  async updateScheduledScan(scanId, updates) {
    try {
      const response = await apiService.put(`/api/antivirus/scheduled/${scanId}`, updates);
      return {
        success: true,
        ...response
      };
    } catch (error) {
      console.error('[AntivirusService] updateScheduledScan failed:', error);
      throw error;
    }
  }

  /**
   * Delete scheduled scan
   */
  async deleteScheduledScan(scanId) {
    try {
      const response = await apiService.delete(`/api/antivirus/scheduled/${scanId}`);
      return {
        success: true,
        ...response
      };
    } catch (error) {
      console.error('[AntivirusService] deleteScheduledScan failed:', error);
      throw error;
    }
  }

  // ============================================
  // VIRUS DATABASE MANAGEMENT
  // ============================================

  /**
   * Get virus database information from all agents
   */
  async getDatabaseInfo() {
    try {
      console.log('[AntivirusService] getDatabaseInfo called');
      
      const response = await apiService.get('/api/antivirus/database-info');
      
      return {
        success: true,
        command_ids: response.command_ids,
        agent_hostnames: response.agent_hostnames,
        message: response.message
      };
    } catch (error) {
      console.error('[AntivirusService] getDatabaseInfo failed:', error);
      throw error;
    }
  }

  /**
   * Get virus database information from a specific agent
   */
  async getAgentDatabaseInfo(agentId) {
    try {
      console.log('[AntivirusService] getAgentDatabaseInfo called with agentId:', agentId);
      
      const response = await apiService.get(`/api/antivirus/database-info/${agentId}`);
      
      return {
        success: true,
        command_id: response.command_id,
        agent_hostname: response.agent_hostname,
        message: response.message
      };
    } catch (error) {
      console.error('[AntivirusService] getAgentDatabaseInfo failed:', error);
      throw error;
    }
  }

  /**
   * Trigger virus database update on agents
   */
  async updateDatabase(agentIds) {
    try {
      console.log('[AntivirusService] updateDatabase called with agentIds:', agentIds);
      
      const response = await apiService.post('/api/antivirus/update-database', {
        agent_ids: agentIds
      });
      
      return {
        success: true,
        command_ids: response.command_ids,
        online_agents: response.online_agents,
        offline_agents: response.offline_agents,
        count: response.count,
        message: response.message,
        warning: response.warning
      };
    } catch (error) {
      console.error('[AntivirusService] updateDatabase failed:', error);
      throw error;
    }
  }

  // ============================================
  // AGENT MANAGEMENT
  // ============================================

  /**
   * Get all agents for scheduling
   */
  async getAgentsForScheduling() {
    try {
      console.log('[AntivirusService] getAgentsForScheduling called');
      const response = await apiService.get('/api/agents');
      
      return {
        success: true,
        agents: response.agents
      };
    } catch (error) {
      console.error('[AntivirusService] getAgentsForScheduling failed:', error);
      throw error;
    }
  }

  /**
 * Get scan history from all agents
 */
async getScanHistory(limit = 50) {
  try {
    console.log('[AntivirusService] getScanHistory called');
    
    const agentsResponse = await apiService.get('/api/agents');
    const scanHistory = [];
    
    for (const agent of agentsResponse.agents) {
      try {
        const history = await apiService.get(`/api/commands/history/${agent.agent_id}`);
        
        // Get all scan commands
        const scanCommands = history.commands.filter(cmd => cmd.type === 'scan');
        
        // DEBUG: Log first scan command structure to see what we're working with
        if (scanCommands.length > 0) {
          console.log('[AntivirusService] Sample scan command structure:', JSON.stringify(scanCommands[0], null, 2));
        }
        
        // Group scan commands by session_id to match start with status updates
        const scanSessions = {};
        
        scanCommands.forEach(cmd => {
          const result = cmd.result || {};
          
          // If this is a scan start command
          if (result.type === 'scan' && result.session_id) {
            if (!scanSessions[result.session_id]) {
              scanSessions[result.session_id] = {
                session_id: result.session_id,
                agent_id: agent.agent_id,
                hostname: agent.hostname,
                command_id: cmd.command_id,
                scan_type: result.scan_type,
                path: result.path,
                started_at: cmd.created_at,
                status: result.status,
                files_scanned: 0,
                threats_found: 0,
                threats: [],
                completed_at: null
              };
            }
          }
          
          // If this is a scan status/completion command
          if (result.type === 'get_scan_status' && result.session_id) {
            const session = scanSessions[result.session_id];
            if (session) {
              session.status = result.status;
              session.files_scanned = result.files_scanned || 0;
              session.threats_found = result.threats_found || 0;
              session.threats = result.threats || [];
              
              if (['completed', 'error', 'cancelled'].includes(result.status)) {
                session.completed_at = cmd.executed_at || cmd.created_at;
              }
            }
          }
        });
        
        // Add all sessions to scan history
        Object.values(scanSessions).forEach(session => {
          scanHistory.push(session);
        });
        
      } catch (err) {
        console.warn(`[AntivirusService] Failed to get scan history for agent ${agent.agent_id}:`, err.message);
      }
    }
    
    // Sort by started_at descending (most recent first)
    scanHistory.sort((a, b) => new Date(b.started_at) - new Date(a.started_at));
    
    // Limit results
    const limitedHistory = scanHistory.slice(0, limit);
    
    console.log('[AntivirusService] Scan history found:', limitedHistory.length);
    console.log('[AntivirusService] Sample history item:', limitedHistory[0]);
    
    return {
      success: true,
      scan_history: limitedHistory,
      total_count: scanHistory.length
    };
  } catch (error) {
    console.error('[AntivirusService] getScanHistory failed:', error);
    throw error;
  }
}
}

const antivirusService = new AntivirusService();
export default antivirusService;