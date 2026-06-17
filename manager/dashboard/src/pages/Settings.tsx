import React from 'react';
import { Container, Row, Col, Card, CardBody, CardHeader } from 'reactstrap';

export const Settings: React.FC = () => {
  document.title = "Settings | RiskNoX Manager";

  return (
    <div className="page-content">
      <Container fluid>
        <Row>
          <Col xs={12}>
            <div className="page-title-box d-sm-flex align-items-center justify-content-between">
              <h4 className="mb-sm-0">Platform Settings</h4>
            </div>
          </Col>
        </Row>

        <Row>
          <Col lg={12}>
            <Card>
              <CardHeader>
                <h4 className="card-title mb-0 flex-grow-1">General Configuration</h4>
              </CardHeader>
              <CardBody>
                <div className="text-center p-5 text-muted">
                  <i className="ri-settings-3-line display-4 mb-3 d-inline-block text-primary"></i>
                  <h5>Settings module is under construction</h5>
                  <p>Configuration options will be available in a future update.</p>
                </div>
              </CardBody>
            </Card>
          </Col>
        </Row>
      </Container>
    </div>
  );
};
