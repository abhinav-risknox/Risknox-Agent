import React, { useState } from 'react';
import { Card, CardBody, Table, Collapse } from 'reactstrap';

interface CommandHistoryTabProps {
  history: any;
}

export const CommandHistoryTab: React.FC<CommandHistoryTabProps> = ({ history }) => {
  const [expandedRows, setExpandedRows] = useState<Record<string, boolean>>({});
  
  const toggleRow = (id: string) => setExpandedRows(prev => ({ ...prev, [id]: !prev[id] }));

  return (
    <Card>
      <CardBody className="p-0">
        <div className="table-responsive table-card">
          <Table className="align-middle table-nowrap mb-0 hover">
            <thead className="table-light">
              <tr className="text-muted text-uppercase tracking-wide fs-11 fw-semibold">
                <th scope="col">ID</th>
                <th scope="col">Command Verb</th>
                <th scope="col">Parameters</th>
                <th scope="col">Status</th>
                <th scope="col">Result</th>
                <th scope="col">Timestamp</th>
              </tr>
            </thead>
            <tbody>
              {history?.commands.map((cmd: any) => (
                <React.Fragment key={cmd.command_id}>
                  <tr 
                    style={{ cursor: cmd.result_payload ? 'pointer' : 'default' }}
                    onClick={() => cmd.result_payload && toggleRow(cmd.command_id)}
                  >
                    <td>
                      <span className="font-monospace text-muted fs-12">{cmd.command_id.split('-').pop()}</span>
                    </td>
                    <td>
                      <div className="d-flex align-items-center gap-2">
                        <i className="ri-terminal-line text-primary fs-14"></i>
                        <span className="fw-semibold">{cmd.verb}</span>
                      </div>
                    </td>
                    <td>
                      <div className="text-muted fs-12 text-truncate" style={{ maxWidth: '200px' }}>
                        {JSON.stringify(cmd.params)}
                      </div>
                    </td>
                    <td>
                      <div className="d-flex align-items-center gap-2">
                        {cmd.status === 'acked' ? (
                          <>
                            <i className="ri-checkbox-circle-line text-success fs-14"></i>
                            <span className="text-success fw-semibold fs-12 text-uppercase">{cmd.ack_status || 'Acknowledged'}</span>
                          </>
                        ) : cmd.status === 'failed' ? (
                          <>
                            <i className="ri-alert-line text-danger fs-14"></i>
                            <span className="text-danger fw-semibold fs-12 text-uppercase">Failed</span>
                          </>
                        ) : (
                          <>
                            <i className="ri-refresh-line text-muted icon-spin fs-14"></i>
                            <span className="text-muted fw-semibold fs-12 text-uppercase">Pending</span>
                          </>
                        )}
                      </div>
                    </td>
                    <td>
                      <span className="text-muted fs-12 fst-italic">
                        {cmd.result_payload ? 'Click to view details' : 'No output'}
                      </span>
                    </td>
                    <td>
                      <div className="d-flex align-items-center justify-content-between">
                        <span className="text-muted fs-12">{cmd.created_at}</span>
                        {cmd.result_payload && (
                          <span className="text-muted ms-2">
                            {expandedRows[cmd.command_id] ? <i className="ri-arrow-up-s-line fs-16"></i> : <i className="ri-arrow-down-s-line fs-16"></i>}
                          </span>
                        )}
                      </div>
                    </td>
                  </tr>
                  <tr>
                    <td colSpan={6} className="p-0 border-0">
                      <Collapse isOpen={expandedRows[cmd.command_id] && !!cmd.result_payload}>
                        <div className="p-3 bg-light-subtle border-bottom">
                          <div className="d-flex align-items-center justify-content-between mb-2">
                            <span className="fs-11 fw-bold text-primary text-uppercase tracking-wide">Command Output Payload</span>
                            <span className="fs-11 text-muted font-monospace">{cmd.command_id}</span>
                          </div>
                          <pre className="fs-12 font-monospace p-3 rounded border overflow-auto" style={{ maxHeight: '400px' }}>
                            {(() => {
                              try {
                                return JSON.stringify(JSON.parse(cmd.result_payload), null, 2);
                              } catch (e) {
                                return cmd.result_payload;
                              }
                            })()}
                          </pre>
                        </div>
                      </Collapse>
                    </td>
                  </tr>
                </React.Fragment>
              ))}
              {(!history?.commands || history.commands.length === 0) && (
                <tr>
                  <td colSpan={6} className="text-center p-4 text-muted">
                    No command history available.
                  </td>
                </tr>
              )}
            </tbody>
          </Table>
        </div>
      </CardBody>
    </Card>
  );
};
