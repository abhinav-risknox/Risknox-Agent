/**
 * Flask Authentication Service
 * Manages Flask API credentials for the current tenant
 */

class FlaskAuthService {
  constructor() {
    this.STORAGE_KEYS = {
      API_URL: 'flaskApiUrl',
      API_KEY: 'flaskApiKey'
    };
  }

  /**
   * Store Flask credentials
   */
  setCredentials(apiUrl, apiKey) {
    if (apiUrl && apiKey) {
      localStorage.setItem(this.STORAGE_KEYS.API_URL, apiUrl);
      localStorage.setItem(this.STORAGE_KEYS.API_KEY, apiKey);
      return true;
    }
    return false;
  }

  /**
   * Get Flask API URL
   */
  getApiUrl() {
    return localStorage.getItem(this.STORAGE_KEYS.API_URL) || 
           process.env.REACT_APP_FLASK_API_URL || 
           '';
  }

  /**
   * Get Flask API Key
   */
  getApiKey() {
    return localStorage.getItem(this.STORAGE_KEYS.API_KEY) || 
           process.env.REACT_APP_API_KEY || 
           '';
  }

  /**
   * Get both credentials
   */
  getCredentials() {
    return {
      apiUrl: this.getApiUrl(),
      apiKey: this.getApiKey()
    };
  }

  /**
   * Check if credentials are configured
   */
  hasCredentials() {
    const url = this.getApiUrl();
    const key = this.getApiKey();
    return !!(url && key);
  }

  /**
   * Clear Flask credentials
   */
  clearCredentials() {
    localStorage.removeItem(this.STORAGE_KEYS.API_URL);
    localStorage.removeItem(this.STORAGE_KEYS.API_KEY);
  }

  /**
   * Make authenticated request to Flask API
   */
  async fetchWithAuth(endpoint, options = {}) {
    const { apiUrl, apiKey } = this.getCredentials();

    if (!apiUrl || !apiKey) {
      throw new Error('Flask API credentials not configured');
    }

    const headers = {
      'Content-Type': 'application/json',
      'X-API-Key': apiKey,
      ...options.headers
    };

    const response = await fetch(`${apiUrl}${endpoint}`, {
      ...options,
      headers
    });

    const data = await response.json();

    if (!response.ok) {
      throw new Error(data.error || data.message || 'Request failed');
    }

    return data;
  }
}

export default new FlaskAuthService();