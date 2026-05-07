import React from 'react';
import { Sidebar } from './Sidebar';
import { Bell, Search } from 'lucide-react';

interface AppShellProps {
  children: React.ReactNode;
}

export const AppShell: React.FC<AppShellProps> = ({ children }) => {
  return (
    <div className="min-h-screen bg-rn-black flex">
      <Sidebar />
      
      <main className="flex-1 ml-64 flex flex-col">
        {/* Topbar */}
        <header className="h-20 border-b border-rn-white/5 flex items-center justify-between px-8 bg-rn-black/50 backdrop-blur-xl sticky top-0 z-40">
          <div className="relative w-96 group">
            <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-rn-white/30 group-focus-within:text-rn-orange transition-colors" />
            <input 
              type="text" 
              placeholder="Search agents, commands, or logs..."
              className="w-full bg-rn-white/5 border border-rn-white/5 rounded-xl py-2.5 pl-10 pr-4 text-sm focus:outline-none focus:ring-2 focus:ring-rn-orange/20 focus:border-rn-orange/20 transition-all"
            />
          </div>

          <div className="flex items-center gap-6">
            <button className="relative p-2 text-rn-white/50 hover:text-rn-white hover:bg-rn-white/5 rounded-full transition-all">
              <Bell className="w-5 h-5" />
              <span className="absolute top-2 right-2 w-2 h-2 bg-rn-orange rounded-full border-2 border-rn-black" />
            </button>
            
            <div className="flex items-center gap-3 pl-6 border-l border-rn-white/10">
              <div className="text-right">
                <p className="text-sm font-bold text-rn-white">Admin</p>
                <p className="text-[10px] text-rn-white/40 uppercase tracking-wider font-bold">Operator</p>
              </div>
              <div className="w-10 h-10 rounded-xl bg-gradient-to-br from-rn-orange to-orange-600 flex items-center justify-center font-bold text-white shadow-lg shadow-rn-orange/20">
                A
              </div>
            </div>
          </div>
        </header>

        {/* Content */}
        <div className="p-8">
          {children}
        </div>
      </main>
    </div>
  );
};
