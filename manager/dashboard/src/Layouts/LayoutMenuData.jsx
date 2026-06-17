// src/layouts/Menu/Navdata.js
import React, { useEffect, useState } from "react";
import { useNavigate } from "react-router-dom";

const Navdata = () => {
  const history = useNavigate();

  const [isAssetsAgents, setIsAssetsAgents] = useState(false);

  const [iscurrentState, setIscurrentState] = useState("");

  const MENU_ITEMS = {
    DASHBOARD: "Dashboard",
    ASSETS_AGENTS: "AssetsAgents",
    SETTINGS: "Settings",

    AGENT_DASHBOARD: "AgentDashboard",
    ASSET_MANAGEMENT: "AssetManagement",
  };

  const ICONS = {
    DASHBOARD: "ri-dashboard-2-line",
    ASSETS_AGENTS: "ri-database-2-line",
    SETTINGS: "ri-settings-3-line",
  };

  const ROUTES = {
    DASHBOARD: "/dashboard",
    AGENT_DASHBOARD: "/agents",
    SETTINGS: "/settings",
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

  useEffect(() => {
    document.body.classList.remove("twocolumn-panel");

    if (iscurrentState !== "AssetsAgents") setIsAssetsAgents(false);
  }, [
    history,
    iscurrentState,
    isAssetsAgents,
  ]);

  const menuItems = [
    { label: "Menu", isHeader: true },
    
    // ===== Dashboard =====
    {
      id: MENU_ITEMS.DASHBOARD,
      label: "Dashboard",
      icon: ICONS.DASHBOARD,
      link: "/",
      click: (e) => {
        setIscurrentState(MENU_ITEMS.DASHBOARD);
      },
    },

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
          id: MENU_ITEMS.AGENT_DASHBOARD,
          label: "Agent Intelligence",
          link: ROUTES.AGENT_DASHBOARD,
          parentId: MENU_ITEMS.ASSETS_AGENTS,
        },
      ],
    },

    // ===== Settings =====
    {
      id: MENU_ITEMS.SETTINGS,
      label: "Settings",
      icon: ICONS.SETTINGS,
      link: ROUTES.SETTINGS,
      click: (e) => {
        setIscurrentState(MENU_ITEMS.SETTINGS);
      },
    },
  ];

  return <>{menuItems}</>;
};

export default Navdata;
