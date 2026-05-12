// Pages
import { Dashboard } from '../pages/Dashboard';
import { Agents } from '../pages/Agents';
import { AgentDetail } from '../pages/AgentDetail';
import { Login } from '../pages/Login';

const authProtectedRoutes = [
  { path: '/', component: <Dashboard /> },
  { path: '/agents', component: <Agents /> },
  { path: '/agents/:id', component: <AgentDetail /> },
  { path: '/commands', component: <div className="text-white p-6">Command Center (Coming Soon)</div> },
  { path: '/audit', component: <div className="text-white p-6">Audit Log (Coming Soon)</div> },
  { path: '/settings', component: <div className="text-white p-6">Settings (Coming Soon)</div> },
];

const publicRoutes = [
  { path: '/login', component: <Login /> },
];

export { authProtectedRoutes, publicRoutes };
