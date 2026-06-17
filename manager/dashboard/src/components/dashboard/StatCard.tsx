import React from 'react';
import { Card, CardBody } from 'reactstrap';

interface StatCardProps {
  title: string;
  value: string | number;
  iconClass: string;
  trend?: string;
  trendUp?: boolean;
  color?: 'warning' | 'primary' | 'success' | 'info' | 'danger';
}

export const StatCard: React.FC<StatCardProps> = ({ 
  title, 
  value, 
  iconClass, 
  trend, 
  trendUp,
  color = 'warning' 
}) => {
  return (
    <Card className="card-animate">
      <CardBody>
        <div className="d-flex align-items-center">
          <div className="flex-grow-1 overflow-hidden">
            <p className="text-uppercase fw-medium text-muted text-truncate mb-0">
              {title}
            </p>
          </div>
        </div>
        <div className="d-flex align-items-end justify-content-between mt-4">
          <div>
            <h4 className="fs-22 fw-semibold ff-secondary mb-4">
              <span className="counter-value">
                {typeof value === 'number' ? value.toLocaleString() : value}
              </span>
            </h4>
            <span className={`badge ${trendUp ? 'bg-success-subtle text-success' : 'bg-danger-subtle text-danger'} me-1`}>
              <i className={`${trendUp ? 'ri-arrow-up-line' : 'ri-arrow-down-line'} align-middle`}></i> {trend || '0%'}
            </span>
            <span className="text-muted">{trend ? 'vs last 24h' : 'Stable status'}</span>
          </div>
          <div className="avatar-sm flex-shrink-0">
            <span className={`avatar-title bg-${color}-subtle rounded fs-3`}>
              <i className={`text-${color} ${iconClass}`}></i>
            </span>
          </div>
        </div>
      </CardBody>
    </Card>
  );
};
