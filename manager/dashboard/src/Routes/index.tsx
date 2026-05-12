import React from 'react';
import { Routes, Route, Navigate } from 'react-router-dom';
import { authProtectedRoutes, publicRoutes } from './allRoutes';
import { VerticalLayout } from '../Layouts/VerticalLayout';

const AuthProtected: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const token = localStorage.getItem('rn_token');
  if (!token) {
    return <Navigate to="/login" replace />;
  }
  return <VerticalLayout>{children}</VerticalLayout>;
};

const Index = () => {
  return (
    <Routes>
      {/* Public Routes */}
      {publicRoutes.map((route, idx) => (
        <Route
          key={idx}
          path={route.path}
          element={route.component}
        />
      ))}

      {/* Auth Protected Routes */}
      {authProtectedRoutes.map((route, idx) => (
        <Route
          key={idx}
          path={route.path}
          element={
            <AuthProtected>
              {route.component}
            </AuthProtected>
          }
        />
      ))}

      {/* Fallback */}
      <Route path="*" element={<Navigate to="/" replace />} />
    </Routes>
  );
};

export default Index;
