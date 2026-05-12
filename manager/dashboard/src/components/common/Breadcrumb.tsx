import React from 'react';
import { Link } from 'react-router-dom';

interface BreadcrumbProps {
  title: string;
  pageTitle: string;
}

export const Breadcrumb: React.FC<BreadcrumbProps> = ({ title, pageTitle }) => {
  return (
    <div className="flex items-center justify-between mb-6">
      <h4 className="text-lg font-bold text-rn-white uppercase tracking-wider">{title}</h4>
      <div className="flex items-center gap-2 text-xs font-medium uppercase tracking-widest">
        <Link to="/" className="text-rn-white/40 hover:text-rn-orange transition-colors">Risknox</Link>
        <span className="text-rn-white/20">/</span>
        <span className="text-rn-white/60">{pageTitle}</span>
      </div>
    </div>
  );
};
