import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Shield, Lock, User, RefreshCw, AlertCircle } from 'lucide-react';
import { endpoints } from '../api/endpoints';

export const Login: React.FC = () => {
  const [username, setUsername] = useState('admin');
  const [password, setPassword] = useState('RiskNoX@2024');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const navigate = useNavigate();

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    setError('');
    
    try {
      const response = await endpoints.login({ username, password });
      localStorage.setItem('rn_token', response.data.token);
      localStorage.setItem('rn_user', response.data.username);
      navigate('/');
    } catch (err: any) {
      setError(err.response?.data?.error || 'Failed to authenticate with Manager API');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen bg-rn-black flex items-center justify-center p-4 relative overflow-hidden">
      {/* Background Decor */}
      <div className="absolute top-0 left-0 w-full h-full">
        <div className="absolute top-[-10%] left-[-10%] w-[40%] h-[40%] bg-rn-orange/10 rounded-full blur-[120px]" />
        <div className="absolute bottom-[-10%] right-[-10%] w-[40%] h-[40%] bg-blue-500/5 rounded-full blur-[120px]" />
      </div>

      <div className="w-full max-w-md relative z-10 animate-in fade-in zoom-in duration-700">
        <div className="text-center mb-10">
          <div className="w-20 h-20 bg-rn-orange rounded-[2.5rem] flex items-center justify-center mx-auto mb-6 shadow-2xl shadow-rn-orange/40 ring-4 ring-rn-orange/20">
            <Shield className="text-white w-10 h-10" />
          </div>
          <h1 className="text-4xl font-display font-bold text-rn-white tracking-tight">
            Risknox<span className="text-rn-orange">.ai</span>
          </h1>
          <p className="text-rn-white/40 mt-2 font-medium">Pulse Management Console</p>
        </div>

        <div className="bg-rn-black-card border border-rn-white/10 rounded-[2.5rem] p-10 shadow-2xl shadow-black/50 backdrop-blur-xl">
          <form onSubmit={handleLogin} className="space-y-6">
            {error && (
              <div className="bg-rn-orange/10 border border-rn-orange/20 p-4 rounded-2xl flex items-center gap-3 text-rn-orange text-sm animate-in shake duration-300">
                <AlertCircle className="w-5 h-5 shrink-0" />
                <p className="font-bold tracking-tight">{error}</p>
              </div>
            )}

            <div className="space-y-2">
              <label className="text-[10px] font-bold text-rn-white/40 uppercase tracking-widest pl-2">Operator ID</label>
              <div className="relative group">
                <User className="absolute left-4 top-1/2 -translate-y-1/2 w-5 h-5 text-rn-white/20 group-focus-within:text-rn-orange transition-colors" />
                <input 
                  type="text" 
                  value={username}
                  onChange={e => setUsername(e.target.value)}
                  className="w-full bg-rn-black border border-rn-white/5 rounded-2xl py-4 pl-12 pr-4 text-rn-white focus:outline-none focus:ring-2 focus:ring-rn-orange/20 focus:border-rn-orange/20 transition-all placeholder:text-rn-white/10"
                  placeholder="Enter username"
                  required
                />
              </div>
            </div>

            <div className="space-y-2">
              <label className="text-[10px] font-bold text-rn-white/40 uppercase tracking-widest pl-2">Access Key</label>
              <div className="relative group">
                <Lock className="absolute left-4 top-1/2 -translate-y-1/2 w-5 h-5 text-rn-white/20 group-focus-within:text-rn-orange transition-colors" />
                <input 
                  type="password" 
                  value={password}
                  onChange={e => setPassword(e.target.value)}
                  className="w-full bg-rn-black border border-rn-white/5 rounded-2xl py-4 pl-12 pr-4 text-rn-white focus:outline-none focus:ring-2 focus:ring-rn-orange/20 focus:border-rn-orange/20 transition-all placeholder:text-rn-white/10"
                  placeholder="Enter password"
                  required
                />
              </div>
            </div>

            <button 
              type="submit" 
              disabled={loading}
              className="w-full bg-rn-orange hover:bg-rn-orange-dim text-white font-bold py-4 rounded-2xl flex items-center justify-center gap-3 transition-all shadow-xl shadow-rn-orange/20 hover:scale-[1.01] active:scale-[0.98] disabled:opacity-50 disabled:hover:scale-100"
            >
              {loading ? <RefreshCw className="w-5 h-5 animate-spin" /> : 'Authenticate System'}
            </button>
          </form>

          <div className="mt-8 pt-8 border-t border-rn-white/5 text-center">
            <p className="text-[10px] text-rn-white/20 font-bold uppercase tracking-[0.2em]">
              Secured by mTLS & RBAC v9
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};
