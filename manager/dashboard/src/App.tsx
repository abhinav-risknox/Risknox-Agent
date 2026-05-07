import React from 'react';
import { BrowserRouter as Router, Routes, Route, Navigate } from 'react-router-dom';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { AppShell } from './components/layout/AppShell';
import { Dashboard } from './pages/Dashboard';
import { Agents } from './pages/Agents';
import { AgentDetail } from './pages/AgentDetail';
import { Login } from './pages/Login';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: 1,
    },
  },
});

// Auth Guard Component
const ProtectedRoute: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const token = localStorage.getItem('rn_token');
  if (!token) {
    return <Navigate to="/login" replace />;
  }
  return <AppShell>{children}</AppShell>;
};

function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <Router>
        <Routes>
          <Route path="/login" element={<Login />} />
          
          <Route path="/" element={
            <ProtectedRoute>
              <Dashboard />
            </ProtectedRoute>
          } />
          
          <Route path="/agents" element={
            <ProtectedRoute>
              <Agents />
            </ProtectedRoute>
          } />
          
          <Route path="/agents/:id" element={
            <ProtectedRoute>
              <AgentDetail />
            </ProtectedRoute>
          } />

          {/* Fallback routes */}
          <Route path="/commands" element={<ProtectedRoute><div className="text-white">Command Center (Coming Soon)</div></ProtectedRoute>} />
          <Route path="/audit" element={<ProtectedRoute><div className="text-white">Audit Log (Coming Soon)</div></ProtectedRoute>} />
          <Route path="/settings" element={<ProtectedRoute><div className="text-white">Settings (Coming Soon)</div></ProtectedRoute>} />
          
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </Router>
    </QueryClientProvider>
  );
}

export default App;
