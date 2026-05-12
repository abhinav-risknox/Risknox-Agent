import React from 'react';
import { NavLink } from 'react-router-dom';
import { 
  LayoutDashboard, 
  Shield, 
  Terminal, 
  History, 
  Settings, 
  Activity,
  ChevronRight
} from 'lucide-react';
import { cn } from '../lib/utils';

const navItems = [
  { icon: LayoutDashboard, label: 'Dashboard', path: '/' },
  { icon: Shield, label: 'Agents', path: '/agents' },
  { icon: Terminal, label: 'Command Center', path: '/commands' },
  { icon: History, label: 'Audit Log', path: '/audit' },
  { icon: Settings, label: 'Settings', path: '/settings' },
];

export const Sidebar: React.FC = () => {
  return (
    <aside className="w-64 h-screen bg-rn-black border-r border-rn-white/5 flex flex-col fixed left-0 top-0 z-50">
      <div className="h-16 flex items-center px-6 gap-3 border-b border-rn-white/5">
        <div className="w-8 h-8 bg-rn-orange rounded-lg flex items-center justify-center shadow-lg shadow-rn-orange/20">
          <Activity className="text-white w-5 h-5" />
        </div>
        <span className="text-xl font-display font-bold tracking-tight text-rn-white">
          Risknox<span className="text-rn-orange">.ai</span>
        </span>
      </div>

      <nav className="flex-1 pl-6 pr-3 py-6 space-y-1 overflow-y-auto">
        <p className="pl-4 text-[10px] text-rn-white/20 uppercase tracking-[0.2em] font-bold mb-4">Menu</p>
        {navItems.map((item) => (
          <NavLink
            key={item.path}
            to={item.path}
            className={({ isActive }) => cn(
              "flex items-center justify-between px-4 py-3 rounded-xl transition-all duration-200 group",
              isActive 
                ? "bg-rn-orange/10 text-rn-orange shadow-[inset_0px_0px_12px_rgba(255,91,0,0.05)]" 
                : "text-rn-white/50 hover:text-rn-white hover:bg-rn-white/5"
            )}
          >
            <div className="flex items-center gap-3 min-w-0">
              <item.icon className="w-5 h-5 shrink-0" />
              <span className="font-medium text-sm truncate">{item.label}</span>
            </div>
            <ChevronRight className={cn(
              "w-4 h-4 transition-transform duration-200 opacity-0 shrink-0",
              "group-hover:opacity-100 group-hover:translate-x-1"
            )} />
          </NavLink>
        ))}
      </nav>

      <div className="p-4 mt-auto">
        <div className="bg-rn-black-card rounded-2xl p-4 border border-rn-white/5">
          <div className="flex items-center gap-3 mb-2">
            <div className="w-2 h-2 rounded-full bg-green-500 animate-pulse" />
            <span className="text-xs font-medium text-rn-white/60">System Online</span>
          </div>
          <p className="text-[10px] text-rn-white/40 uppercase tracking-widest font-bold">
            Version 1.0.0
          </p>
        </div>
      </div>
    </aside>
  );
};
