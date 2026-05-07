import { useQuery } from '@tanstack/react-query';
import { endpoints } from '../api/endpoints';

export const useAgents = () => {
  return useQuery({
    queryKey: ['agents'],
    queryFn: async () => {
      const response = await endpoints.agents.list();
      return response.data;
    },
    refetchInterval: 10000, // Refresh every 10 seconds
  });
};
