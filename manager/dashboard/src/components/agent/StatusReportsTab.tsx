import React from 'react';
import { Card, CardHeader, CardBody } from 'reactstrap';

interface StatusReportsTabProps {
  statusReports: any;
}

export const StatusReportsTab: React.FC<StatusReportsTabProps> = ({ statusReports }) => {
  return (
    <div className="space-y-4">
      {statusReports?.reports.map((report: any, idx: number) => (
        <Card key={idx} className="mb-4">
          <CardHeader className="d-flex align-items-center border-bottom-0 pb-0 pt-4 px-4">
            <div className="d-flex align-items-center gap-3 flex-grow-1">
              <div className="avatar-sm flex-shrink-0">
                <div className="avatar-title bg-light text-primary rounded fs-18">
                  <i className="ri-file-code-line fs-20"></i>
                </div>
              </div>
              <div>
                <h6 className="fs-14 fw-bold text-uppercase mb-1 tracking-wide">{report.report_type}</h6>
                <p className="text-muted fs-11 mb-0">{report.created_at}</p>
              </div>
            </div>
            <button className="btn btn-sm btn-ghost-secondary btn-icon">
              <i className="ri-external-link-line fs-16"></i>
            </button>
          </CardHeader>
          <CardBody className="p-4">
            <pre className="bg-light p-3 rounded fs-12 font-monospace overflow-auto border" style={{ maxHeight: '400px' }}>
              {JSON.stringify(report.report_data, null, 2)}
            </pre>
          </CardBody>
        </Card>
      ))}
      
      {(!statusReports || statusReports.reports.length === 0) && (
        <div className="text-center p-5 border border-dashed rounded text-muted">
          <i className="ri-file-code-line fs-32 mx-auto mb-3 text-secondary d-block"></i>
          <h6 className="text-uppercase fw-semibold fs-12 tracking-wide">No health reports available for this endpoint</h6>
        </div>
      )}
    </div>
  );
};
