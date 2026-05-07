import React from 'react';
import type { LucideIcon } from 'lucide-react';
import { cn } from '../../lib/utils';

interface StatCardProps {
  title: string;
  value: string | number;
  icon: LucideIcon;
  trend?: string;
  trendUp?: boolean;
  color?: 'orange' | 'blue' | 'green' | 'purple';
}

export const StatCard: React.FC<StatCardProps> = ({ 
  title, 
  value, 
  icon: Icon, 
  trend, 
  trendUp,
  color = 'orange' 
}) => {
  const colorMap = {
    orange: 'from-rn-orange to-orange-600 shadow-rn-orange/20 text-rn-orange',
    blue: 'from-blue-500 to-blue-700 shadow-blue-500/20 text-blue-500',
    green: 'from-green-500 to-green-700 shadow-green-500/20 text-green-500',
    purple: 'from-purple-500 to-purple-700 shadow-purple-500/20 text-purple-500',
  };

  return (
    <div className="bg-rn-black-card border border-rn-white/5 rounded-3xl p-6 hover:border-rn-white/10 transition-all group overflow-hidden relative">
      <div className="absolute -right-4 -top-4 w-24 h-24 bg-rn-orange/5 rounded-full blur-3xl group-hover:bg-rn-orange/10 transition-colors" />
      
      <div className="flex items-start justify-between relative z-10">
        <div>
          <p className="text-rn-white/40 text-sm font-bold uppercase tracking-wider mb-1">{title}</p>
          <h3 className="text-4xl font-display font-bold text-rn-white tracking-tight">{value}</h3>
          
          {trend && (
            <div className={cn(
              "flex items-center gap-1 mt-2 text-xs font-bold",
              trendUp ? "text-green-500" : "text-rn-orange"
            )}>
              {trendUp ? '↑' : '↓'} {trend}
              <span className="text-rn-white/20 font-medium">vs last 24h</span>
            </div>
          )}
        </div>

        <div className={cn(
          "w-12 h-12 rounded-2xl flex items-center justify-center bg-rn-white/5 border border-rn-white/5 group-hover:border-rn-orange/20 transition-all",
          colorMap[color].split(' ').pop() // Use the color text class
        )}>
          <Icon className="w-6 h-6" />
        </div>
      </div>
    </div>
  );
};
