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
    orange: 'text-rn-orange border-rn-orange/20 hover:shadow-[0_8px_16px_rgba(255,91,0,0.15)]',
    blue: 'text-blue-500 border-blue-500/20 hover:shadow-[0_8px_16px_rgba(59,130,246,0.15)]',
    green: 'text-green-500 border-green-500/20 hover:shadow-[0_8px_16px_rgba(34,197,94,0.15)]',
    purple: 'text-purple-500 border-purple-500/20 hover:shadow-[0_8px_16px_rgba(168,85,247,0.15)]',
  };

  return (
    <div className={cn(
      "bg-transparent border rounded-xl p-6 transition-all duration-300 cursor-pointer min-h-[140px] flex flex-col h-full group hover:-translate-y-1",
      colorMap[color]
    )}>
      <div className="flex items-center justify-between mb-auto">
        <p className="text-[11px] font-medium text-rn-white/40 uppercase tracking-[0.5px]">
          {title}
        </p>
        <div className={cn("text-2xl opacity-80 group-hover:opacity-100 transition-opacity")}>
          <Icon className="w-8 h-8" />
        </div>
      </div>
      
      <div className="flex items-end justify-between mt-4">
        <div>
          <h4 className="text-2xl font-display font-semibold text-rn-white mb-1">
            {value}
          </h4>
          <p className="text-[12px] text-rn-white/30">
            {trend && (
              <span className={cn(
                "font-bold mr-1",
                trendUp ? "text-green-500" : "text-rn-orange"
              )}>
                {trendUp ? '↑' : '↓'} {trend}
              </span>
            )}
            {trend ? 'vs last 24h' : 'Stable status'}
          </p>
        </div>
      </div>
    </div>
  );
};
