// src/layouts/Menu/Navdata.js
import React, { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";

const Navdata = () => {
  const history = useNavigate();

  // Top-level (collapsible) groups state constants
  const [isThreatIntelligence, setIsThreatIntelligence] = useState(false);
  const [isInvestigate, setIsInvestigate] = useState(false);
  const [isPostureExposure, setIsPostureExposure] = useState(false);
  const [isSettings, setIsSettings] = useState(false);
  const [isAssetsAgents, setIsAssetsAgents] = useState(false);
  const [isPatchManagement, setIsPatchManagement] = useState(false);
  const [isAntivirusDashboard, setIsAntivirusDashboard] = useState(false);
  const [isVulnerabilityManagement, setIsVulnerabilityManagement] = useState(false);
  const [isWebBlocking, setIsWebBlocking] = useState(false);
  const [isSoftwareBlocking, setIsSoftwareBlocking] = useState(false);
  const [isCompliance, setIsCompliance] = useState(false);
  const [isDetect, setIsDetect] = useState(false);
  const [isCollaboration, setIsCollaboration] = useState(false);
  // const [isSOARDashboard, setIsSOARDashboard] = useState(false);
  // const [isConnectors, setIsConnectors] = useState(false);
  const [isSecurityAnalytics, setIsSecurityAnalytics] = useState(false);
  // const [isOTSecurity, setIsOTSecurity] = useState(false);

  const [iscurrentState, setIscurrentState] = useState("");

  // Menu item constants
  const MENU_ITEMS = {
    // Main Categories
    THREAT_INTELLIGENCE: "ThreatIntelligence",
    POSTURE_EXPOSURE: "PostureExposure",
    INVESTIGATE: "Investigate",
    SETTINGS: "Settings",
    ASSETS_AGENTS: "AssetsAgents",
    PATCH_MANAGEMENT: "PatchManagement",
    ANTIVIRUS_DASHBOARD: "AntivirusDashboard",
    VULNERABILITY_MANAGEMENT: "VulnerabilityManagement",
    WEB_BLOCKING: "WebBlocking",
    SOFTWARE_BLOCKING: "SoftwareBlocking",
    COMPLIANCE: "Compliance",
    DETECT: "Detect",
    COLLABORATION: "Collaboration",
    // SOAR_DASHBOARD: "SOARDashboard",
    // CONNECTORS: "Connectors",
    SECURITY_ANALYTICS: "SecurityAnalytics",
    REPORTS: "Reports",
    // OT_SECURITY: "OTSecurity",

    // Sub Items
    THREAT_FEED: "ThreatFeed",
    THREAT_SUMMARY: "ThreatSummary",
    AGENT_DASHBOARD: "AgentDashboard",
    POSTURE_SUMMARY: "PostureSummary",
    CONTROLS_POSTURE: "ControlsPosture",
    MISCONFIGURATIONS: "Misconfigurations",
    ALERTS_MAP: "AlertsMap",
    ASSET_TIMELINE: "AssetTimeline",
    TAGS_FILTERS: "TagsFilters",
    ASSET_ANALYTICS: "AssetAnalytics",
    ASSET_MANAGEMENT: "AssetManagement",
    AGENT_CONFIGURATION: "AgentConfiguration",
    PATCH_ANALYTICS: "PatchAnalytics",
    ANTIVIRUS_ANALYTICS: "AntivirusAnalytics",
    VULNERABILITY_ANALYTICS: "VulnerabilityAnalytics",
    WEB_BLOCKING_ANALYTICS: "WebBlockingAnalytics",
    SOFTWARE_BLOCKING_ANALYTICS: "SoftwareBlockingAnalytics",
    COMPLIANCE_SHIELD: "ComplianceShield",
    RULE_MANAGEMENT: "RuleManagement",
    TEAM_MANAGEMENT: "TeamManagement",
    TASK_MANAGEMENT: "TaskManagement",
    // SOAR Sub Items
    // INCIDENT_ANALYTICS: "IncidentAnalytics",
    // INCIDENT_OVERVIEW: "IncidentOverview",
    // INCIDENT_MANAGER: "IncidentManager",
    // INCIDENT_SUMMARY: "IncidentSummary",
    // INCIDENT_INTEGRATIONS: "Integrations",
    // OT Security Sub Items
    // OT_SECURITY_DASHBOARD: "OTSecurityDashboard",
    // Settings Sub Items
    LOG_RETENTION: "LogRetention",
  };

  // Icon constants
  const ICONS = {
    THREAT_INTELLIGENCE: "ri-radar-line",
    POSTURE_EXPOSURE: "ri-shield-line",
    INVESTIGATE: "ri-search-eye-line",
    SETTINGS: "ri-settings-3-line",
    ASSETS_AGENTS: "ri-database-2-line",
    PATCH_MANAGEMENT: "ri-tools-line",
    ANTIVIRUS_DASHBOARD: "ri-bug-line",
    VULNERABILITY_MANAGEMENT: "ri-shield-keyhole-line",
    WEB_BLOCKING: "ri-forbid-line",
    SOFTWARE_BLOCKING: "ri-apps-line",
    COMPLIANCE: "ri-verified-badge-line",
    DETECT: "ri-alarm-warning-line",
    COLLABORATION: "ri-team-line",
    // SOAR_DASHBOARD: "ri-layout-grid-line",
    // CONNECTORS: "ri-plug-line",
    // OT_SECURITY: "ri-cpu-line",
  };

  // Route constants
  const ROUTES = {
    THREAT_FEED: "/ThreatFeed",
    THREAT_GUARD: "/ThreatGuard",
    AGENT_DASHBOARD: "/AgentDashboard",
    POSTURE_SUMMARY: "/PostureSummary",
    COMPLIANCE: "/Compliance",
    MISCONFIGURATION: "/Misconfiguration",
    ALERTS_MAP: "/AlertsMap",
    ASSET_TIMELINE: "/AssetTimeline",
    TAGS_FILTERS: "/TagsFilters",
    ASSET_ANALYTICS: "/AgentControl",
    ASSET_MANAGEMENT: "/AssetManagement",
    AGENT_CONFIGURATION: "/AgentConfiguration",
    PATCH_MANAGEMENT: "/PatchManagement",
    ANTIVIRUS_DASHBOARD: "/AntivirusDashboard",
    VULNERABILITY_MANAGEMENT: "/VulnerabilityManagement",
    WEB_BLOCKING: "/WebBlocking",
    SOFTWARE_BLOCKING: "/SoftwareBlocking",
    COMPLIANCE_SHIELD: "/ComplianceShield",
    ACTIVE_ALERTS: "/ActiveAlerts",
    ADD_USER: "/AddUser",
    KANBAN_BOARD: "/KanbanBoard",
    // SOAR Routes
    // INCIDENT_ANALYTICS: "/IncidentAnalytics",
    // INCIDENT_OVERVIEW: "/IncidentOverview",
    // INCIDENT_MANAGER: "/IncidentManager",
    // INCIDENT_SUMMARY: "/IncidentSummary",
    // INTEGRATIONS: "/Integrations",
    // Connectors
    // CONNECTORS: "/Connectors",
    SECURITY_ANALYTICS_REPORTS: "/agent-report",
    // OT Security
    // OT_SECURITY: "/OTSecurity",
    // Settings Routes
    LOG_RETENTION: "/Settings/LogRetention",
  };

  function updateIconSidebar(e) {
    if (e && e.target && e.target.getAttribute("subitems")) {
      const ul = document.getElementById("two-column-menu");
      if (!ul) return;
      const iconItems = ul.querySelectorAll(".nav-icon.active");
      [...iconItems].forEach((item) => {
        item.classList.remove("active");
        const id = item.getAttribute("subitems");
        const el = document.getElementById(id);
        if (el) el.classList.remove("show");
      });
    }
  }

  // Close other sections when one opens
  useEffect(() => {
    document.body.classList.remove("twocolumn-panel");

    if (iscurrentState !== "ThreatIntelligence") setIsThreatIntelligence(false);
    if (iscurrentState !== "Investigate") setIsInvestigate(false);
    if (iscurrentState !== "PostureExposure") setIsPostureExposure(false);
    if (iscurrentState !== "Settings") setIsSettings(false);
    if (iscurrentState !== "AssetsAgents") setIsAssetsAgents(false);
    if (iscurrentState !== "PatchManagement") setIsPatchManagement(false);
    if (iscurrentState !== "AntivirusDashboard") setIsAntivirusDashboard(false);
    if (iscurrentState !== "VulnerabilityManagement") setIsVulnerabilityManagement(false);
    if (iscurrentState !== "WebBlocking") setIsWebBlocking(false);
    if (iscurrentState !== "SoftwareBlocking") setIsSoftwareBlocking(false);
    if (iscurrentState !== "Compliance") setIsCompliance(false);
    if (iscurrentState !== "Detect") setIsDetect(false);
    if (iscurrentState !== "Collaboration") setIsCollaboration(false);
    // if (iscurrentState !== "SOARDashboard") setIsSOARDashboard(false);
    // if (iscurrentState !== "Connectors") setIsConnectors(false);
    // if (iscurrentState !== "OTSecurity") setIsOTSecurity(false);
  }, [
    history,
    iscurrentState,
    isThreatIntelligence,
    isInvestigate,
    isPostureExposure,
    isSettings,
    isAssetsAgents,
    isPatchManagement,
    isAntivirusDashboard,
    isVulnerabilityManagement,
    isWebBlocking,
    isSoftwareBlocking,
    isCompliance,
    isDetect,
    isCollaboration,
    // isSOARDashboard,
    // isConnectors,
    isSecurityAnalytics,
    // isOTSecurity,
  ]);

  const menuItems = [
    { label: "Menu", isHeader: true },

    // ===== Threat Intelligence =====
    {
      id: MENU_ITEMS.THREAT_INTELLIGENCE,
      label: "Dashboard",
      icon: ICONS.THREAT_INTELLIGENCE,
      link: "/#",
      stateVariables: isThreatIntelligence,
      click: (e) => {
        e.preventDefault();
        setIsThreatIntelligence(!isThreatIntelligence);
        setIscurrentState(MENU_ITEMS.THREAT_INTELLIGENCE);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.THREAT_FEED,
          label: "Threat Feed",
          link: ROUTES.THREAT_FEED,
          parentId: MENU_ITEMS.THREAT_INTELLIGENCE,
        },
      ],
    },

    // ===== Posture & Exposure =====
    {
      id: MENU_ITEMS.POSTURE_EXPOSURE,
      label: "Posture & Exposure",
      icon: ICONS.POSTURE_EXPOSURE,
      link: "/#",
      stateVariables: isPostureExposure,
      click: (e) => {
        e.preventDefault();
        setIsPostureExposure(!isPostureExposure);
        setIscurrentState(MENU_ITEMS.POSTURE_EXPOSURE);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.POSTURE_SUMMARY,
          label: "Posture Summary",
          link: ROUTES.POSTURE_SUMMARY,
          parentId: MENU_ITEMS.POSTURE_EXPOSURE,
        },
      ],
    },

    // ===== Investigate =====
    {
      id: MENU_ITEMS.INVESTIGATE,
      label: "Investigate",
      icon: ICONS.INVESTIGATE,
      link: "/#",
      stateVariables: isInvestigate,
      click: (e) => {
        e.preventDefault();
        setIsInvestigate(!isInvestigate);
        setIscurrentState(MENU_ITEMS.INVESTIGATE);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.ALERTS_MAP,
          label: "Maps & Topology",
          link: ROUTES.ALERTS_MAP,
          parentId: MENU_ITEMS.INVESTIGATE,
        },
      ],
    },

    // ===== SOAR Dashboard ===== (commented out)
    // {
    //   id: MENU_ITEMS.SOAR_DASHBOARD,
    //   label: "SOAR Dashboard",
    //   icon: ICONS.SOAR_DASHBOARD,
    //   link: "/#",
    //   stateVariables: isSOARDashboard,
    //   click: (e) => {
    //     e.preventDefault();
    //     setIsSOARDashboard(!isSOARDashboard);
    //     setIscurrentState(MENU_ITEMS.SOAR_DASHBOARD);
    //     updateIconSidebar(e);
    //   },
    //   subItems: [
    //     {
    //       id: MENU_ITEMS.INCIDENT_ANALYTICS,
    //       label: "Incident Analytics",
    //       link: ROUTES.INCIDENT_ANALYTICS,
    //       parentId: MENU_ITEMS.SOAR_DASHBOARD,
    //     },
    //     {
    //       id: MENU_ITEMS.INCIDENT_OVERVIEW,
    //       label: "Incident Overview",
    //       link: ROUTES.INCIDENT_OVERVIEW,
    //       parentId: MENU_ITEMS.SOAR_DASHBOARD,
    //     },
    //     {
    //       id: MENU_ITEMS.INCIDENT_MANAGER,
    //       label: "Incident Manager",
    //       link: ROUTES.INCIDENT_MANAGER,
    //       parentId: MENU_ITEMS.SOAR_DASHBOARD,
    //     },
    //     {
    //       id: MENU_ITEMS.INCIDENT_SUMMARY,
    //       label: "Incident Summary",
    //       link: ROUTES.INCIDENT_SUMMARY,
    //       parentId: MENU_ITEMS.SOAR_DASHBOARD,
    //     },
    //     {
    //       id: MENU_ITEMS.INCIDENT_INTEGRATIONS,
    //       label: "Integrations",
    //       link: ROUTES.INTEGRATIONS,
    //       parentId: MENU_ITEMS.SOAR_DASHBOARD,
    //     },
    //   ],
    // },

    // ===== OT Security ===== (commented out)
    // {
    //   id: MENU_ITEMS.OT_SECURITY,
    //   label: "OT Security",
    //   icon: ICONS.OT_SECURITY,
    //   link: "/#",
    //   stateVariables: isOTSecurity,
    //   click: (e) => {
    //     e.preventDefault();
    //     setIsOTSecurity(!isOTSecurity);
    //     setIscurrentState(MENU_ITEMS.OT_SECURITY);
    //     updateIconSidebar(e);
    //   },
    //   subItems: [
    //     {
    //       id: MENU_ITEMS.OT_SECURITY_DASHBOARD,
    //       label: "OT Security Dashboard",
    //       link: ROUTES.OT_SECURITY,
    //       parentId: MENU_ITEMS.OT_SECURITY,
    //     },
    //   ],
    // },

    // ===== Connectors ===== (commented out)
    // {
    //   id: MENU_ITEMS.CONNECTORS,
    //   label: "Connectors",
    //   icon: ICONS.CONNECTORS,
    //   link: "/#",
    //   stateVariables: isConnectors,
    //   click: (e) => {
    //     e.preventDefault();
    //     setIsConnectors(!isConnectors);
    //     setIscurrentState(MENU_ITEMS.CONNECTORS);
    //     updateIconSidebar(e);
    //   },
    //   subItems: [
    //     {
    //       id: MENU_ITEMS.CONNECTORS,
    //       label: "Connectors",
    //       link: ROUTES.CONNECTORS,
    //       parentId: MENU_ITEMS.CONNECTORS,
    //     },
    //   ],
    // },

    // ===== Assets & Agents =====
    {
      id: MENU_ITEMS.ASSETS_AGENTS,
      label: "Assets & Agents",
      icon: ICONS.ASSETS_AGENTS,
      link: "/#",
      stateVariables: isAssetsAgents,
      click: (e) => {
        e.preventDefault();
        setIsAssetsAgents(!isAssetsAgents);
        setIscurrentState(MENU_ITEMS.ASSETS_AGENTS);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.ASSET_MANAGEMENT,
          label: "Asset Management",
          link: ROUTES.ASSET_MANAGEMENT,
          parentId: MENU_ITEMS.ASSETS_AGENTS,
        },
        {
          id: MENU_ITEMS.AGENT_DASHBOARD,
          label: "Agent Intelligence",
          link: ROUTES.AGENT_DASHBOARD,
          parentId: MENU_ITEMS.ASSETS_AGENTS,
        },
      ],
    },

    // ===== Antivirus Dashboard =====
    {
      id: MENU_ITEMS.ANTIVIRUS_DASHBOARD,
      label: "Antivirus Dashboard",
      icon: ICONS.ANTIVIRUS_DASHBOARD,
      link: "/#",
      stateVariables: isAntivirusDashboard,
      click: (e) => {
        e.preventDefault();
        setIsAntivirusDashboard(!isAntivirusDashboard);
        setIscurrentState(MENU_ITEMS.ANTIVIRUS_DASHBOARD);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.ANTIVIRUS_ANALYTICS,
          label: "Antivirus Analytics",
          link: ROUTES.ANTIVIRUS_DASHBOARD,
          parentId: MENU_ITEMS.ANTIVIRUS_DASHBOARD,
        },
      ],
    },

    // ===== Vulnerability Management =====
    {
      id: MENU_ITEMS.VULNERABILITY_MANAGEMENT,
      label: "Vulnerability Management",
      icon: ICONS.VULNERABILITY_MANAGEMENT,
      link: "/#",
      stateVariables: isVulnerabilityManagement,
      click: (e) => {
        e.preventDefault();
        setIsVulnerabilityManagement(!isVulnerabilityManagement);
        setIscurrentState(MENU_ITEMS.VULNERABILITY_MANAGEMENT);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.VULNERABILITY_ANALYTICS,
          label: "Vulnerability Analytics",
          link: ROUTES.VULNERABILITY_MANAGEMENT,
          parentId: MENU_ITEMS.VULNERABILITY_MANAGEMENT,
        },
      ],
    },

    // ===== Compliance =====
    {
      id: MENU_ITEMS.COMPLIANCE,
      label: "Compliance",
      icon: ICONS.COMPLIANCE,
      link: "/#",
      stateVariables: isCompliance,
      click: (e) => {
        e.preventDefault();
        setIsCompliance(!isCompliance);
        setIscurrentState(MENU_ITEMS.COMPLIANCE);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.COMPLIANCE_SHIELD,
          label: "Compliance Shield",
          link: ROUTES.COMPLIANCE_SHIELD,
          parentId: MENU_ITEMS.COMPLIANCE,
        },
      ],
    },

    // ===== Detect =====
    {
      id: MENU_ITEMS.DETECT,
      label: "Detect",
      icon: ICONS.DETECT,
      link: "/#",
      stateVariables: isDetect,
      click: (e) => {
        e.preventDefault();
        setIsDetect(!isDetect);
        setIscurrentState(MENU_ITEMS.DETECT);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.RULE_MANAGEMENT,
          label: "Detections & Rules",
          link: ROUTES.ACTIVE_ALERTS,
          parentId: MENU_ITEMS.DETECT,
        },
      ],
    },

    // ===== Security Analytics =====
    {
      id: MENU_ITEMS.SECURITY_ANALYTICS,
      label: "Security Analytics",
      icon: ICONS.DETECT,
      link: "/#",
      stateVariables: isSecurityAnalytics,
      click: (e) => {
        e.preventDefault();
        setIsSecurityAnalytics(!isSecurityAnalytics);
        setIscurrentState(MENU_ITEMS.SECURITY_ANALYTICS);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.REPORTS,
          label: "Reports",
          link: ROUTES.SECURITY_ANALYTICS_REPORTS,
          parentId: MENU_ITEMS.SECURITY_ANALYTICS,
        },
      ],
    },

    // ===== Settings =====
    {
      id: MENU_ITEMS.SETTINGS,
      label: "Settings",
      icon: ICONS.SETTINGS,
      link: "/#",
      stateVariables: isSettings,
      click: (e) => {
        e.preventDefault();
        setIsSettings(!isSettings);
        setIscurrentState(MENU_ITEMS.SETTINGS);
        updateIconSidebar(e);
      },
      subItems: [
        {
          id: MENU_ITEMS.LOG_RETENTION,
          label: "Log Retention",
          link: ROUTES.LOG_RETENTION,
          parentId: MENU_ITEMS.SETTINGS,
        },
      ],
    },
  ];

  return <>{menuItems}</>;
};

export default Navdata;
