import React from 'react';
import { Sidebar } from './Sidebar';
import { Header } from './Header';

interface VerticalLayoutProps {
  children: React.ReactNode;
}

export const VerticalLayout: React.FC<VerticalLayoutProps> = ({ children }) => {
  return (
    <div id="layout-wrapper" className="min-h-screen bg-rn-black">
      <Header />
      <Sidebar />
      <main className="main-content pl-72 pt-16 min-h-screen transition-all duration-300">
        <div className="page-content p-6">
          <div className="container-fluid">
            {children}
          </div>
        </div>
        <footer className="footer h-14 border-t border-rn-white/5 flex items-center px-6 text-rn-white/30 text-xs">
          <div className="container-fluid flex justify-between w-full">
            <span>2026 © Risknox.ai</span>
            <span>Design & Develop by Risknox Team</span>
          </div>
        </footer>
      </main>
    </div>
  );
};
