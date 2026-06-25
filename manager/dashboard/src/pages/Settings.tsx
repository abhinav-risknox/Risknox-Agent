import React, { useState, useEffect } from 'react';
import { Container, Row, Col, Card, CardBody, CardHeader, Button, Input, Label, Form, FormGroup, Spinner, Alert, Progress } from 'reactstrap';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { endpoints, type SettingsResponse } from '../api/endpoints';

export const Settings: React.FC = () => {
  document.title = "Settings | RiskNoX Manager";
  const queryClient = useQueryClient();

  const [maxAgents, setMaxAgents] = useState<number>(100);
  const [saveSuccess, setSaveSuccess] = useState<boolean>(false);
  const [saveError, setSaveError] = useState<string | null>(null);

  const { data, isLoading, isError } = useQuery<SettingsResponse>({
    queryKey: ['settings'],
    queryFn: async () => {
      const res = await endpoints.settings.get();
      return res.data;
    }
  });

  useEffect(() => {
    if (data) {
      setMaxAgents(data.max_agents);
    }
  }, [data]);

  const mutation = useMutation({
    mutationFn: (newLimit: number) => endpoints.settings.update({ max_agents: newLimit }),
    onSuccess: () => {
      setSaveSuccess(true);
      setSaveError(null);
      queryClient.invalidateQueries({ queryKey: ['settings'] });
      queryClient.invalidateQueries({ queryKey: ['agents'] });
      setTimeout(() => setSaveSuccess(false), 3000);
    },
    onError: (error: any) => {
      setSaveSuccess(false);
      setSaveError(error.response?.data?.error || "Failed to update settings.");
    }
  });

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    mutation.mutate(maxAgents);
  };

  if (isLoading) {
    return (
      <div className="page-content">
        <Container fluid>
          <div className="text-center mt-5">
            <Spinner color="primary" />
          </div>
        </Container>
      </div>
    );
  }

  if (isError) {
    return (
      <div className="page-content">
        <Container fluid>
          <Alert color="danger">Failed to load settings.</Alert>
        </Container>
      </div>
    );
  }

  const currentCount = data?.current_agent_count || 0;
  const limit = data?.max_agents || 100;
  const usagePercent = Math.min(100, Math.round((currentCount / limit) * 100)) || 0;
  
  let progressColor = "success";
  if (usagePercent >= 100) progressColor = "danger";
  else if (usagePercent >= 80) progressColor = "warning";

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
          <Col lg={6}>
            <Card>
              <CardHeader>
                <h4 className="card-title mb-0 flex-grow-1">Agent Configuration</h4>
              </CardHeader>
              <CardBody>
                {saveSuccess && <Alert color="success">Settings saved successfully.</Alert>}
                {saveError && <Alert color="danger">{saveError}</Alert>}
                
                <div className="mb-4">
                  <h5 className="fs-14 mb-2">Agent License Usage</h5>
                  <div className="d-flex justify-content-between mb-1">
                    <span className="text-muted">Registered Agents</span>
                    <span className="fw-medium">{currentCount} / {limit}</span>
                  </div>
                  <Progress value={usagePercent} color={progressColor} style={{ height: '8px' }} />
                  {usagePercent >= 100 && (
                    <div className="text-danger mt-2 fs-12">
                      <i className="ri-error-warning-line align-middle me-1"></i>
                      Agent limit reached. New agents will be rejected.
                    </div>
                  )}
                </div>

                <Form onSubmit={handleSubmit}>
                  <FormGroup>
                    <Label htmlFor="maxAgentsInput">Maximum Allowed Agents</Label>
                    <Input 
                      type="number" 
                      id="maxAgentsInput" 
                      value={maxAgents} 
                      onChange={(e) => setMaxAgents(parseInt(e.target.value) || 0)} 
                      min="0"
                      required
                    />
                    <small className="form-text text-muted">
                      New agent registrations will be rejected once this limit is reached. Existing agents can still reconnect.
                    </small>
                  </FormGroup>
                  <div className="text-end mt-3">
                    <Button color="primary" type="submit" disabled={mutation.isPending}>
                      {mutation.isPending ? <Spinner size="sm" className="me-2" /> : <i className="ri-save-line align-middle me-1"></i>}
                      Save Changes
                    </Button>
                  </div>
                </Form>
              </CardBody>
            </Card>
          </Col>
        </Row>
      </Container>
    </div>
  );
};
