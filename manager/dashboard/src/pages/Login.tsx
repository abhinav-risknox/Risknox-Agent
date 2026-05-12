import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Lock } from 'lucide-react';
import { endpoints } from '../api/endpoints';

const login = async (username: string, password: string) => {
  return endpoints.login({ username, password }).then(res => res.data);
};

export const Login: React.FC = () => {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const navigate = useNavigate();

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsLoading(true);
    setError('');
    
    try {
      const response = await login(username, password);
      localStorage.setItem('rn_token', response.token);
      localStorage.setItem('rn_user', username);
      navigate('/');
    } catch (err: any) {
      setError(err.response?.data?.error || 'Invalid username or password');
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="min-h-screen flex items-center justify-center relative overflow-hidden" style={{ background: "radial-gradient(circle, #150605 0%, #0b0101 100%)" }}>
      {/* Animated background elements */}
      <div className="absolute top-[-10%] left-[-10%] w-[40%] h-[40%] bg-rn-orange/5 rounded-full blur-[120px] animate-pulse" />
      <div className="absolute bottom-[-10%] right-[-10%] w-[40%] h-[40%] bg-rn-orange/5 rounded-full blur-[120px] animate-pulse delay-700" />

      <div className="container mx-auto px-4 z-10">
        <div className="flex items-center justify-center">
          {/* Right: Login Form */}
          <div className="w-full max-w-md animate-in fade-in zoom-in duration-1000">
            <div className="bg-[#150605]/90 border border-rn-orange/10 rounded-3xl p-8 lg:p-10 shadow-2xl backdrop-blur-xl">
              <div className="text-center mb-10">
                <div className="inline-flex items-center justify-center w-16 h-16 rounded-2xl bg-rn-orange/10 text-rn-orange mb-6 border border-rn-orange/20">
                  <Lock className="w-8 h-8" />
                </div>
                <h1 className="text-3xl font-display font-bold text-rn-white mb-2">🔐 Secure Login</h1>
                <p className="text-[#a88a7a] text-sm">Access your Risknox Dashboard</p>
              </div>

              {error && (
                <div className="bg-red-500/10 border border-red-500/20 text-red-400 px-4 py-3 rounded-xl text-sm mb-6 animate-in shake duration-300">
                  {error}
                </div>
              )}

              <form onSubmit={handleLogin} className="space-y-6">
                <div className="space-y-2">
                  <label className="text-xs font-bold text-rn-white/40 uppercase tracking-widest ml-1">Username</label>
                  <input
                    type="text"
                    value={username}
                    onChange={(e) => setUsername(e.target.value)}
                    className="w-full bg-[#110404] border border-[#3d2b2a] rounded-xl px-4 py-3 text-rn-white focus:outline-none focus:border-rn-orange/50 transition-all placeholder:text-rn-white/10"
                    placeholder="Enter your username"
                    required
                  />
                </div>

                <div className="space-y-2">
                  <div className="flex items-center justify-between ml-1">
                    <label className="text-xs font-bold text-rn-white/40 uppercase tracking-widest">Password</label>
                    <a href="#" className="text-xs font-bold text-rn-orange hover:underline">Forgot password?</a>
                  </div>
                  <input
                    type="password"
                    value={password}
                    onChange={(e) => setPassword(e.target.value)}
                    className="w-full bg-[#110404] border border-[#3d2b2a] rounded-xl px-4 py-3 text-rn-white focus:outline-none focus:border-rn-orange/50 transition-all placeholder:text-rn-white/10"
                    placeholder="Enter your password"
                    required
                  />
                </div>

                <div className="flex items-center gap-2 ml-1">
                  <input type="checkbox" id="remember" className="rounded border-rn-white/10 bg-rn-white/5 text-rn-orange focus:ring-rn-orange" />
                  <label htmlFor="remember" className="text-xs text-rn-white/40 font-medium cursor-pointer">Remember me</label>
                </div>

                <button
                  type="submit"
                  disabled={isLoading}
                  className="w-full bg-[#d45d00] hover:bg-[#b04d00] text-white font-bold py-4 rounded-xl shadow-lg shadow-rn-orange/20 transition-all hover:scale-[1.01] active:scale-[0.99] disabled:opacity-50 disabled:hover:scale-100 flex items-center justify-center gap-2"
                >
                  {isLoading ? <div className="w-5 h-5 border-2 border-white/30 border-t-white rounded-full animate-spin" /> : "Sign In"}
                </button>
              </form>
            </div>
            
            <p className="text-center mt-8 text-rn-white/20 text-xs">
              © 2026 Risknox.ai. All rights reserved.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};
