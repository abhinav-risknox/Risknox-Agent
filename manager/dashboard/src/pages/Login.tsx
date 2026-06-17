import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';

import { endpoints } from '../api/endpoints';
import { Container, Row, Col, Card, CardBody, Input, Label, Button, Form, Alert, Spinner } from 'reactstrap';

const login = async (username: string, password: string) => {
  return endpoints.login({ username, password }).then(res => res.data);
};

export const Login: React.FC = () => {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const navigate = useNavigate();

  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    setIsLoading(true);
    setError('');
    
    try {
      const response = await login(username, password);
      localStorage.setItem('rn_token', response.token);
      localStorage.setItem('rn_user', username);
      navigate('/');
    } catch (err: any) {
      setError(err.response?.data?.error || 'Invalid username or password');
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="auth-page-wrapper pt-5">
      <div className="auth-one-bg-position auth-one-bg" id="auth-particles">
        <div className="bg-overlay"></div>
        <div className="shape">
          <svg xmlns="http://www.w3.org/2000/svg" version="1.1" xmlnsXlink="http://www.w3.org/1999/xlink" viewBox="0 0 1440 120">
            <path d="M 0,36 C 144,53.6 432,123.2 720,124 C 1008,124.8 1296,56.8 1440,40L1440 140L0 140z"></path>
          </svg>
        </div>
      </div>

      <div className="auth-page-content">
        <Container>
          <Row>
            <Col lg={12}>
              <div className="text-center mt-sm-5 mb-4 text-white-50">
                <div>
                  <h1 className="text-white mb-2">RiskNoX</h1>
                </div>
                <p className="mt-3 fs-15 fw-medium">Pulse Security Platform</p>
              </div>
            </Col>
          </Row>

          <Row className="justify-content-center">
            <Col md={8} lg={6} xl={5}>
              <Card className="mt-4">
                <CardBody className="p-4">
                  <div className="text-center mt-2">
                    <h5 className="text-primary">Welcome Back !</h5>
                    <p className="text-muted">Sign in to continue to Risknox Manager.</p>
                  </div>
                  
                  {error && (
                    <Alert color="danger" className="border-0 mb-4 animate-in fade-in">
                      {error}
                    </Alert>
                  )}

                  <div className="p-2 mt-4">
                    <Form onSubmit={handleLogin}>
                      <div className="mb-3">
                        <Label htmlFor="username" className="form-label">Username</Label>
                        <Input
                          type="text"
                          className="form-control"
                          id="username"
                          placeholder="Enter username"
                          value={username}
                          onChange={(e) => setUsername(e.target.value)}
                          required
                        />
                      </div>

                      <div className="mb-3">
                        <div className="float-end">
                          <a href="#" className="text-muted">Forgot password?</a>
                        </div>
                        <Label className="form-label" htmlFor="password-input">Password</Label>
                        <div className="position-relative auth-pass-inputgroup mb-3">
                          <Input
                            type="password"
                            className="form-control pe-5 password-input"
                            placeholder="Enter password"
                            id="password-input"
                            value={password}
                            onChange={(e) => setPassword(e.target.value)}
                            required
                          />
                        </div>
                      </div>

                      <div className="form-check">
                        <Input className="form-check-input" type="checkbox" value="" id="auth-remember-check" />
                        <Label className="form-check-label" htmlFor="auth-remember-check">Remember me</Label>
                      </div>

                      <div className="mt-4">
                        <Button color="success" className="w-100" type="submit" disabled={isLoading}>
                          {isLoading ? <Spinner size="sm" className="me-2" /> : null}
                          Sign In
                        </Button>
                      </div>
                    </Form>
                  </div>
                </CardBody>
              </Card>

              <div className="mt-4 text-center">
                <p className="mb-0 text-muted">© {new Date().getFullYear()} Risknox.ai. All rights reserved.</p>
              </div>
            </Col>
          </Row>
        </Container>
      </div>
    </div>
  );
};
