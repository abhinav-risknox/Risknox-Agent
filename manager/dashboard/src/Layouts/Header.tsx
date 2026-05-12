import React, { useState } from 'react';
import { Bell, Search, User, LogOut, Settings, HelpCircle } from 'lucide-react';

export const Header: React.FC = () => {
  const [isProfileOpen, setIsProfileOpen] = useState(false);

  return (
    <header className="h-16 fixed top-0 right-0 left-72 bg-rn-black/80 backdrop-blur-md border-b border-rn-white/5 z-40 flex items-center justify-between px-6">
      <div className="flex items-center gap-4 flex-1">
        <div className="relative group max-w-md w-full">
          <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-rn-white/30 group-focus-within:text-rn-orange transition-colors" />
          <input 
            type="text" 
            placeholder="Search for agents, logs, or threats..." 
            className="w-full bg-rn-white/5 border border-rn-white/10 rounded-xl py-2 pl-10 pr-4 text-sm focus:outline-none focus:ring-1 focus:ring-rn-orange/50 transition-all"
          />
        </div>
      </div>

      <div className="flex items-center gap-4">
        <button className="relative p-2 text-rn-white/50 hover:text-rn-white hover:bg-rn-white/5 rounded-xl transition-all">
          <Bell className="w-5 h-5" />
          <span className="absolute top-2 right-2 w-2 h-2 bg-rn-orange rounded-full border-2 border-rn-black" />
        </button>

        <div className="relative">
          <button 
            onClick={() => setIsProfileOpen(!isProfileOpen)}
            className="flex items-center gap-3 p-1.5 pr-3 rounded-xl hover:bg-rn-white/5 transition-all group"
          >
            <div className="w-8 h-8 rounded-lg bg-rn-orange/10 flex items-center justify-center text-rn-orange border border-rn-orange/20">
              <User className="w-5 h-5" />
            </div>
            <div className="text-left hidden sm:block">
              <p className="text-xs font-bold text-rn-white group-hover:text-rn-orange transition-colors">Admin User</p>
              <p className="text-[10px] text-rn-white/40 uppercase tracking-tighter">Super Admin</p>
            </div>
          </button>

          {isProfileOpen && (
            <div className="absolute top-full right-0 mt-2 w-56 bg-rn-black-card border border-rn-white/10 rounded-2xl shadow-2xl p-2 z-50 animate-in fade-in slide-in-from-top-2">
              <div className="px-4 py-3 border-b border-rn-white/5 mb-2">
                <p className="text-xs text-rn-white/40">Welcome Admin!</p>
              </div>
              
              <button className="w-full flex items-center gap-3 px-3 py-2 rounded-xl text-sm text-rn-white/60 hover:text-rn-white hover:bg-rn-white/5 transition-all">
                <User className="w-4 h-4" /> Profile
              </button>
              <button className="w-full flex items-center gap-3 px-3 py-2 rounded-xl text-sm text-rn-white/60 hover:text-rn-white hover:bg-rn-white/5 transition-all">
                <Settings className="w-4 h-4" /> Settings
              </button>
              <button className="w-full flex items-center gap-3 px-3 py-2 rounded-xl text-sm text-rn-white/60 hover:text-rn-white hover:bg-rn-white/5 transition-all">
                <HelpCircle className="w-4 h-4" /> Help Center
              </button>
              
              <div className="h-px bg-rn-white/5 my-2" />
              
              <button 
                onClick={() => {
                  localStorage.removeItem('rn_token');
                  window.location.href = '/login';
                }}
                className="w-full flex items-center gap-3 px-3 py-2 rounded-xl text-sm text-red-400 hover:bg-red-400/10 transition-all"
              >
                <LogOut className="w-4 h-4" /> Logout
              </button>
            </div>
          )}
        </div>
      </div>
    </header>
  );
};
