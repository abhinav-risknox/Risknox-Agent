// ============================================
// frontend/src/services/apiService.js
// ============================================

class ApiService {
  constructor() {
    // Don't hardcode baseURL - get it dynamically
    this.STORAGE_KEYS = {
      API_URL: 'flaskApiUrl',
      API_KEY: 'flaskApiKey'
    };
  }

  /**
   * Get Flask credentials from localStorage with fallback to env variables
   */
  getFlaskCredentials() {
    return {
      apiUrl: localStorage.getItem(this.STORAGE_KEYS.API_URL),
      apiKey: localStorage.getItem(this.STORAGE_KEYS.API_KEY)
    };
  }

  /**
   * Check if Flask credentials are configured
   */
  hasCredentials() {
    const { apiUrl, apiKey } = this.getFlaskCredentials();
    return !!(apiUrl && apiKey);
  }

  /**
   * Validate credentials before making request
   */
  validateCredentials() {
    const { apiUrl, apiKey } = this.getFlaskCredentials();
    
    if (!apiUrl || !apiKey) {
      throw new Error(
        'Flask API credentials not configured. Please login again to access this feature.'
      );
    }
    
    return { apiUrl, apiKey };
  }

  /**
   * Core request method with dynamic credentials
   */
  async request(endpoint, options = {}) {
    // Validate and get credentials
    const { apiUrl, apiKey } = this.validateCredentials();
    
    const url = `${apiUrl}${endpoint}`;
    
    const config = {
      headers: {
        'Content-Type': 'application/json',
        'X-API-Key': apiKey,
        ...options.headers
      },
      ...options
    };

    try {
      console.log(`API Request: ${options.method || 'GET'} ${endpoint}`);
      
      const response = await fetch(url, config);
      
      const contentType = response.headers.get('content-type');
      let data;
      
      if (contentType && contentType.includes('application/json')) {
        data = await response.json();
      } else {
        data = await response.text();
      }
      
      if (!response.ok) {
        // Handle authentication errors specifically
        if (response.status === 401 || response.status === 403) {
          const authError = new Error(
            'Authentication failed. Please check your credentials or login again.'
          );
          authError.status = response.status;
          authError.isAuthError = true;
          throw authError;
        }
        
        const errorMessage = typeof data === 'object' 
          ? (data.error || data.message) 
          : data;
        
        const error = new Error(
          errorMessage || `HTTP ${response.status}: ${response.statusText}`
        );
        error.status = response.status;
        error.response = data;
        throw error;
      }
      
      return data;
    } catch (error) {
      console.error(`API call failed for ${endpoint}:`, error);
      
      // Handle network errors
      if (error instanceof TypeError && error.message.includes('fetch')) {
        throw new Error('Network error: Unable to connect to Flask server');
      }
      
      // Handle credential errors
      if (error.message.includes('credentials not configured')) {
        throw error;
      }
      
      // Handle authentication errors
      if (error.isAuthError) {
        throw error;
      }
      
      throw error;
    }
  }

  /**
   * GET request
   */
  get(endpoint, params = {}) {
    const queryString = Object.keys(params).length
      ? '?' + new URLSearchParams(params).toString()
      : '';
    
    return this.request(endpoint + queryString, {
      method: 'GET'
    });
  }

  /**
   * POST request
   */
  post(endpoint, body = {}) {
    return this.request(endpoint, {
      method: 'POST',
      body: JSON.stringify(body)
    });
  }

  /**
   * PUT request
   */
  put(endpoint, body = {}) {
    return this.request(endpoint, {
      method: 'PUT',
      body: JSON.stringify(body)
    });
  }

  /**
   * DELETE request
   */
  delete(endpoint) {
    return this.request(endpoint, {
      method: 'DELETE'
    });
  }

  /**
   * PATCH request
   */
  patch(endpoint, body = {}) {
    return this.request(endpoint, {
      method: 'PATCH',
      body: JSON.stringify(body)
    });
  }

  /**
   * Upload file (multipart/form-data)
   */
  async upload(endpoint, formData) {
    const { apiUrl, apiKey } = this.validateCredentials();
    
    const url = `${apiUrl}${endpoint}`;
    
    try {
      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'X-API-Key': apiKey
          // Don't set Content-Type - browser will set it with boundary
        },
        body: formData
      });

      const data = await response.json();

      if (!response.ok) {
        throw new Error(data.error || data.message || 'Upload failed');
      }

      return data;
    } catch (error) {
      console.error(`Upload failed for ${endpoint}:`, error);
      throw error;
    }
  }

  /**
   * Download file (returns blob)
   */
  async download(endpoint, params = {}) {
    const { apiUrl, apiKey } = this.validateCredentials();
    
    const queryString = Object.keys(params).length
      ? '?' + new URLSearchParams(params).toString()
      : '';
    
    const url = `${apiUrl}${endpoint}${queryString}`;
    
    try {
      const response = await fetch(url, {
        method: 'GET',
        headers: {
          'X-API-Key': apiKey
        }
      });

      if (!response.ok) {
        const data = await response.json();
        throw new Error(data.error || data.message || 'Download failed');
      }

      return await response.blob();
    } catch (error) {
      console.error(`Download failed for ${endpoint}:`, error);
      throw error;
    }
  }

  /**
   * Set credentials (called during login)
   */
  setCredentials(apiUrl, apiKey) {
    if (apiUrl && apiKey) {
      localStorage.setItem(this.STORAGE_KEYS.API_URL, apiUrl);
      localStorage.setItem(this.STORAGE_KEYS.API_KEY, apiKey);
      console.log('Flask credentials configured:', { apiUrl, hasKey: !!apiKey });
      return true;
    }
    console.warn('Attempted to set invalid credentials');
    return false;
  }

  /**
   * Clear credentials (called during logout)
   */
  clearCredentials() {
    localStorage.removeItem(this.STORAGE_KEYS.API_URL);
    localStorage.removeItem(this.STORAGE_KEYS.API_KEY);
    console.log('Flask credentials cleared');
  }

  /**
   * Get current API URL (for debugging/display)
   */
  getCurrentApiUrl() {
    const { apiUrl } = this.getFlaskCredentials();
    return apiUrl;
  }

  /**
   * Check if credentials are valid by making a test request
   */
  async validateConnection() {
    try {
      // Try to fetch a basic endpoint to validate credentials
      await this.get('/api/health'); // Adjust endpoint as needed
      return { valid: true, message: 'Connection successful' };
    } catch (error) {
      return { 
        valid: false, 
        message: error.message || 'Connection failed',
        isAuthError: error.isAuthError || false
      };
    }
  }
}

// Export singleton instance
const apiService = new ApiService();
export default apiService;