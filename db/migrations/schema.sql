CREATE FUNCTION public.set_module_command_expiry() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    NEW.expires_at := NEW.created_at + INTERVAL '30 days';
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.set_module_command_expiry() OWNER TO postgres;

--
-- TOC entry 233 (class 1255 OID 25615)
-- Name: set_policy_command_expiry(); Type: FUNCTION; Schema: public; Owner: postgres
--

CREATE FUNCTION public.set_policy_command_expiry() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
BEGIN
    NEW.expires_at := NEW.created_at + INTERVAL '30 days';
    RETURN NEW;
END;
$$;


ALTER FUNCTION public.set_policy_command_expiry() OWNER TO postgres;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- TOC entry 226 (class 1259 OID 25538)
-- Name: agent_status_reports; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.agent_status_reports (
    id integer NOT NULL,
    agent_id text NOT NULL,
    report_type text NOT NULL,
    report_data jsonb NOT NULL,
    created_at timestamp with time zone DEFAULT now()
);


ALTER TABLE public.agent_status_reports OWNER TO postgres;

--
-- TOC entry 225 (class 1259 OID 25537)
-- Name: agent_status_reports_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.agent_status_reports_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.agent_status_reports_id_seq OWNER TO postgres;

--
-- TOC entry 5106 (class 0 OID 0)
-- Dependencies: 225
-- Name: agent_status_reports_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.agent_status_reports_id_seq OWNED BY public.agent_status_reports.id;


--
-- TOC entry 220 (class 1259 OID 16389)
-- Name: agents; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.agents (
    id integer NOT NULL,
    agent_id character varying(128) NOT NULL,
    hostname character varying(256) NOT NULL,
    os_type character varying(64) NOT NULL,
    os_version character varying(128),
    agent_version character varying(32),
    status character varying(32) DEFAULT 'ACTIVE'::character varying NOT NULL,
    cert_serial character varying(128),
    registered_at timestamp with time zone DEFAULT now() NOT NULL,
    last_seen_at timestamp with time zone,
    ip_address character varying(45),
    CONSTRAINT chk_agent_status CHECK (((status)::text = ANY ((ARRAY['ACTIVE'::character varying, 'INACTIVE'::character varying, 'REVOKED'::character varying, 'PENDING'::character varying])::text[])))
);


ALTER TABLE public.agents OWNER TO postgres;

--
-- TOC entry 219 (class 1259 OID 16388)
-- Name: agents_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.agents_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.agents_id_seq OWNER TO postgres;

--
-- TOC entry 5107 (class 0 OID 0)
-- Dependencies: 219
-- Name: agents_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.agents_id_seq OWNED BY public.agents.id;


--
-- TOC entry 222 (class 1259 OID 16411)
-- Name: certificates; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.certificates (
    id integer NOT NULL,
    serial_number character varying(128) NOT NULL,
    agent_id character varying(128) NOT NULL,
    certificate_pem text NOT NULL,
    issued_at timestamp with time zone DEFAULT now() NOT NULL,
    expires_at timestamp with time zone NOT NULL,
    revoked boolean DEFAULT false NOT NULL,
    revoked_at timestamp with time zone,
    revoke_reason character varying(256)
);


ALTER TABLE public.certificates OWNER TO postgres;

--
-- TOC entry 221 (class 1259 OID 16410)
-- Name: certificates_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.certificates_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.certificates_id_seq OWNER TO postgres;

--
-- TOC entry 5108 (class 0 OID 0)
-- Dependencies: 221
-- Name: certificates_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.certificates_id_seq OWNED BY public.certificates.id;


--
-- TOC entry 224 (class 1259 OID 16439)
-- Name: licenses; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.licenses (
    id integer NOT NULL,
    agent_id character varying(128) NOT NULL,
    license_key character varying(256) NOT NULL,
    license_type character varying(64) DEFAULT 'TRIAL'::character varying NOT NULL,
    valid_from timestamp with time zone DEFAULT now() NOT NULL,
    valid_until timestamp with time zone NOT NULL,
    max_agents integer DEFAULT 1 NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    CONSTRAINT chk_license_type CHECK (((license_type)::text = ANY ((ARRAY['TRIAL'::character varying, 'STANDARD'::character varying, 'ENTERPRISE'::character varying])::text[])))
);


ALTER TABLE public.licenses OWNER TO postgres;

--
-- TOC entry 223 (class 1259 OID 16438)
-- Name: licenses_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.licenses_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.licenses_id_seq OWNER TO postgres;

--
-- TOC entry 5109 (class 0 OID 0)
-- Dependencies: 223
-- Name: licenses_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.licenses_id_seq OWNED BY public.licenses.id;


--
-- TOC entry 230 (class 1259 OID 25587)
-- Name: module_commands; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.module_commands (
    id integer NOT NULL,
    agent_id character varying(255) NOT NULL,
    command_id text DEFAULT ''::text NOT NULL,
    verb character varying(100) NOT NULL,
    params jsonb DEFAULT '{}'::jsonb NOT NULL,
    status character varying(20) DEFAULT 'pending'::character varying NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    dispatched_at timestamp with time zone,
    ack_status text,
    result_payload text,
    ack_at timestamp with time zone,
    expires_at timestamp with time zone
);


ALTER TABLE public.module_commands OWNER TO postgres;

--
-- TOC entry 231 (class 1259 OID 25610)
-- Name: module_command_log; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.module_command_log AS
 SELECT id,
    agent_id,
    command_id,
    verb,
    params,
    status AS dispatch_status,
    ack_status,
    result_payload,
    created_at,
    dispatched_at,
    ack_at
   FROM public.module_commands
  ORDER BY created_at DESC;


ALTER VIEW public.module_command_log OWNER TO postgres;

--
-- TOC entry 229 (class 1259 OID 25586)
-- Name: module_commands_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.module_commands_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.module_commands_id_seq OWNER TO postgres;

--
-- TOC entry 5110 (class 0 OID 0)
-- Dependencies: 229
-- Name: module_commands_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.module_commands_id_seq OWNED BY public.module_commands.id;


--
-- TOC entry 228 (class 1259 OID 25566)
-- Name: policy_commands; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.policy_commands (
    id integer NOT NULL,
    agent_id character varying(255) NOT NULL,
    command_id text DEFAULT ''::text NOT NULL,
    policy_type character varying(50) NOT NULL,
    policy_data jsonb NOT NULL,
    status character varying(20) DEFAULT 'pending'::character varying NOT NULL,
    created_at timestamp with time zone DEFAULT now() NOT NULL,
    dispatched_at timestamp with time zone,
    ack_status text,
    ack_message text,
    ack_at timestamp with time zone,
    expires_at timestamp with time zone
);


ALTER TABLE public.policy_commands OWNER TO postgres;

--
-- TOC entry 227 (class 1259 OID 25565)
-- Name: policy_commands_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.policy_commands_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.policy_commands_id_seq OWNER TO postgres;

--
-- TOC entry 5111 (class 0 OID 0)
-- Dependencies: 227
-- Name: policy_commands_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.policy_commands_id_seq OWNED BY public.policy_commands.id;


--
-- TOC entry 232 (class 1259 OID 25620)
-- Name: stale_pending_commands; Type: VIEW; Schema: public; Owner: postgres
--

CREATE VIEW public.stale_pending_commands AS
 SELECT 'policy'::text AS command_class,
    policy_commands.id,
    policy_commands.agent_id,
    policy_commands.command_id,
    policy_commands.policy_type AS verb_or_type,
    policy_commands.status,
    policy_commands.created_at,
    policy_commands.expires_at
   FROM public.policy_commands
  WHERE (((policy_commands.status)::text = 'pending'::text) AND (policy_commands.expires_at < now()))
UNION ALL
 SELECT 'module'::text AS command_class,
    module_commands.id,
    module_commands.agent_id,
    module_commands.command_id,
    module_commands.verb AS verb_or_type,
    module_commands.status,
    module_commands.created_at,
    module_commands.expires_at
   FROM public.module_commands
  WHERE (((module_commands.status)::text = 'pending'::text) AND (module_commands.expires_at < now()))
  ORDER BY 7;


ALTER VIEW public.stale_pending_commands OWNER TO postgres;

--
-- TOC entry 4902 (class 2604 OID 25541)
-- Name: agent_status_reports id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.agent_status_reports ALTER COLUMN id SET DEFAULT nextval('public.agent_status_reports_id_seq'::regclass);


--
-- TOC entry 4891 (class 2604 OID 16392)
-- Name: agents id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.agents ALTER COLUMN id SET DEFAULT nextval('public.agents_id_seq'::regclass);


--
-- TOC entry 4894 (class 2604 OID 16414)
-- Name: certificates id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.certificates ALTER COLUMN id SET DEFAULT nextval('public.certificates_id_seq'::regclass);


--
-- TOC entry 4897 (class 2604 OID 16442)
-- Name: licenses id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.licenses ALTER COLUMN id SET DEFAULT nextval('public.licenses_id_seq'::regclass);


--
-- TOC entry 4908 (class 2604 OID 25590)
-- Name: module_commands id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.module_commands ALTER COLUMN id SET DEFAULT nextval('public.module_commands_id_seq'::regclass);


--
-- TOC entry 4904 (class 2604 OID 25569)
-- Name: policy_commands id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.policy_commands ALTER COLUMN id SET DEFAULT nextval('public.policy_commands_id_seq'::regclass);


--
-- TOC entry 4934 (class 2606 OID 25550)
-- Name: agent_status_reports agent_status_reports_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.agent_status_reports
    ADD CONSTRAINT agent_status_reports_pkey PRIMARY KEY (id);


--
-- TOC entry 4916 (class 2606 OID 16407)
-- Name: agents agents_agent_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.agents
    ADD CONSTRAINT agents_agent_id_key UNIQUE (agent_id);


--
-- TOC entry 4918 (class 2606 OID 16405)
-- Name: agents agents_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.agents
    ADD CONSTRAINT agents_pkey PRIMARY KEY (id);


--
-- TOC entry 4922 (class 2606 OID 16427)
-- Name: certificates certificates_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.certificates
    ADD CONSTRAINT certificates_pkey PRIMARY KEY (id);


--
-- TOC entry 4924 (class 2606 OID 16429)
-- Name: certificates certificates_serial_number_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.certificates
    ADD CONSTRAINT certificates_serial_number_key UNIQUE (serial_number);


--
-- TOC entry 4930 (class 2606 OID 16459)
-- Name: licenses licenses_license_key_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.licenses
    ADD CONSTRAINT licenses_license_key_key UNIQUE (license_key);


--
-- TOC entry 4932 (class 2606 OID 16457)
-- Name: licenses licenses_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.licenses
    ADD CONSTRAINT licenses_pkey PRIMARY KEY (id);


--
-- TOC entry 4946 (class 2606 OID 25605)
-- Name: module_commands module_commands_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.module_commands
    ADD CONSTRAINT module_commands_pkey PRIMARY KEY (id);


--
-- TOC entry 4942 (class 2606 OID 25583)
-- Name: policy_commands policy_commands_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.policy_commands
    ADD CONSTRAINT policy_commands_pkey PRIMARY KEY (id);


--
-- TOC entry 4919 (class 1259 OID 16408)
-- Name: idx_agents_agent_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_agents_agent_id ON public.agents USING btree (agent_id);


--
-- TOC entry 4920 (class 1259 OID 16409)
-- Name: idx_agents_status; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_agents_status ON public.agents USING btree (status);


--
-- TOC entry 4925 (class 1259 OID 16435)
-- Name: idx_certificates_agent_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_certificates_agent_id ON public.certificates USING btree (agent_id);


--
-- TOC entry 4926 (class 1259 OID 16437)
-- Name: idx_certificates_revoked; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_certificates_revoked ON public.certificates USING btree (revoked);


--
-- TOC entry 4927 (class 1259 OID 16436)
-- Name: idx_certificates_serial; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_certificates_serial ON public.certificates USING btree (serial_number);


--
-- TOC entry 4928 (class 1259 OID 16465)
-- Name: idx_licenses_agent_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_licenses_agent_id ON public.licenses USING btree (agent_id);


--
-- TOC entry 4943 (class 1259 OID 25617)
-- Name: idx_module_commands_command_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_module_commands_command_id ON public.module_commands USING btree (command_id) WHERE (command_id <> ''::text);


--
-- TOC entry 4944 (class 1259 OID 25606)
-- Name: idx_module_commands_pending; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_module_commands_pending ON public.module_commands USING btree (agent_id, status) WHERE ((status)::text = 'pending'::text);


--
-- TOC entry 4939 (class 1259 OID 25614)
-- Name: idx_policy_commands_command_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE UNIQUE INDEX idx_policy_commands_command_id ON public.policy_commands USING btree (command_id) WHERE (command_id <> ''::text);


--
-- TOC entry 4940 (class 1259 OID 25584)
-- Name: idx_policy_commands_pending; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_policy_commands_pending ON public.policy_commands USING btree (agent_id, status) WHERE ((status)::text = 'pending'::text);


--
-- TOC entry 4935 (class 1259 OID 25556)
-- Name: idx_status_reports_agent; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_status_reports_agent ON public.agent_status_reports USING btree (agent_id, created_at DESC);


--
-- TOC entry 4936 (class 1259 OID 25609)
-- Name: idx_status_reports_agent_type; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_status_reports_agent_type ON public.agent_status_reports USING btree (agent_id, report_type, created_at DESC);


--
-- TOC entry 4937 (class 1259 OID 25608)
-- Name: idx_status_reports_retention; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_status_reports_retention ON public.agent_status_reports USING btree (created_at);


--
-- TOC entry 4938 (class 1259 OID 25557)
-- Name: idx_status_reports_type; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_status_reports_type ON public.agent_status_reports USING btree (report_type);


--
-- TOC entry 4951 (class 2620 OID 25619)
-- Name: module_commands trg_module_command_expiry; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_module_command_expiry BEFORE INSERT ON public.module_commands FOR EACH ROW EXECUTE FUNCTION public.set_module_command_expiry();


--
-- TOC entry 4950 (class 2620 OID 25616)
-- Name: policy_commands trg_policy_command_expiry; Type: TRIGGER; Schema: public; Owner: postgres
--

CREATE TRIGGER trg_policy_command_expiry BEFORE INSERT ON public.policy_commands FOR EACH ROW EXECUTE FUNCTION public.set_policy_command_expiry();


--
-- TOC entry 4949 (class 2606 OID 25551)
-- Name: agent_status_reports agent_status_reports_agent_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.agent_status_reports
    ADD CONSTRAINT agent_status_reports_agent_id_fkey FOREIGN KEY (agent_id) REFERENCES public.agents(agent_id);


--
-- TOC entry 4947 (class 2606 OID 16430)
-- Name: certificates certificates_agent_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.certificates
    ADD CONSTRAINT certificates_agent_id_fkey FOREIGN KEY (agent_id) REFERENCES public.agents(agent_id);


--
-- TOC entry 4948 (class 2606 OID 16460)
-- Name: licenses licenses_agent_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.licenses
    ADD CONSTRAINT licenses_agent_id_fkey FOREIGN KEY (agent_id) REFERENCES public.agents(agent_id);

--
-- Manager settings (key-value store for platform configuration)
--

CREATE TABLE IF NOT EXISTS public.manager_settings (
    key character varying(64) NOT NULL,
    value jsonb NOT NULL,
    updated_at timestamp with time zone DEFAULT now() NOT NULL,
    CONSTRAINT manager_settings_pkey PRIMARY KEY (key)
);

ALTER TABLE public.manager_settings OWNER TO postgres;

-- Seed default agent limit
INSERT INTO public.manager_settings (key, value) VALUES ('max_agents', '100')
    ON CONFLICT (key) DO NOTHING;
