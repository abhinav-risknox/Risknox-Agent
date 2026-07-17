import React, { useState } from 'react';
import { ComposableMap, Geographies, Geography, Marker, ZoomableGroup } from 'react-simple-maps';
import { Tooltip } from 'react-tooltip';
import { useNavigate } from 'react-router-dom';
import type { Agent } from '../../api/endpoints';

const geoUrl = "https://cdn.jsdelivr.net/npm/world-atlas@2/countries-110m.json";

interface WorldMapProps {
  data: {
    country_code: string;
    country: string;
    count: number;
    lat?: number;
    lon?: number;
  }[];
  agents?: Agent[];
}

export const WorldMap: React.FC<WorldMapProps> = ({ data, agents = [] }) => {
  const navigate = useNavigate();
  const [tooltipContent, setTooltipContent] = useState("");
  const [position, setPosition] = useState({ coordinates: [0, 30], zoom: 1 });

  const highlightedCountries = data.map(d => d.country_code.toUpperCase());

  const handleZoomIn = () => {
    if (position.zoom >= 8) return;
    setPosition((pos) => ({ ...pos, zoom: pos.zoom * 1.5 }));
  };

  const handleZoomOut = () => {
    if (position.zoom <= 1) {
      setPosition({ coordinates: [0, 30], zoom: 1 });
      return;
    }
    setPosition((pos) => ({ ...pos, zoom: pos.zoom / 1.5 }));
  };

  const handleMoveEnd = (position: any) => {
    setPosition(position);
  };

  return (
    <div style={{ width: "100%", height: "400px", position: "relative", marginBottom: "1rem" }}>
      <div className="position-absolute top-0 end-0 m-3 z-3 d-flex flex-column gap-1">
        <button className="btn btn-sm btn-light shadow-sm" onClick={handleZoomIn} title="Zoom In">
          <i className="ri-add-line"></i>
        </button>
        <button className="btn btn-sm btn-light shadow-sm" onClick={handleZoomOut} title="Zoom Out">
          <i className="ri-subtract-line"></i>
        </button>
      </div>
      <ComposableMap
        projection="geoMercator"
        projectionConfig={{
          scale: 150,
        }}
        style={{
          width: "100%",
          height: "100%",
          backgroundColor: "transparent"
        }}
      >
        <ZoomableGroup
          zoom={position.zoom}
          center={position.coordinates as [number, number]}
          onMove={handleMoveEnd}
        >
          <Geographies geography={geoUrl}>
            {({ geographies }) =>
              geographies.map((geo) => {
                const countryCode = geo.properties.ISO_A2;
                const isHighlighted = highlightedCountries.includes(countryCode);
                const stats = data.find(d => d.country_code.toUpperCase() === countryCode);

                return (
                  <Geography
                    key={geo.rsmKey}
                    geography={geo}
                    onMouseEnter={() => {
                      if (isHighlighted && stats) {
                        setTooltipContent(`${stats.country}: ${stats.count} agents`);
                      }
                    }}
                    onMouseLeave={() => setTooltipContent("")}
                    style={{
                      default: {
                        fill: isHighlighted ? "var(--vz-primary-bg-subtle)" : "var(--vz-light)",
                        outline: "none",
                        stroke: "var(--vz-border-color)",
                        strokeWidth: 0.5 / position.zoom,
                        transition: "fill 250ms"
                      },
                      hover: {
                        fill: isHighlighted ? "var(--vz-primary)" : "var(--vz-border-color)",
                        outline: "none",
                        cursor: isHighlighted ? "pointer" : "default"
                      },
                      pressed: {
                        fill: isHighlighted ? "var(--vz-primary)" : "var(--vz-light)",
                        outline: "none",
                      },
                    }}
                    data-tooltip-id="map-tooltip"
                  />
                );
              })
            }
          </Geographies>
          {agents.map((agent) => {
            if (agent.geo?.lon !== undefined && agent.geo?.lat !== undefined) {
              return (
                <Marker key={`agent-${agent.agent_id}`} coordinates={[agent.geo.lon, agent.geo.lat]}>
                  <circle 
                    r={4 / position.zoom} 
                    fill="var(--vz-success)" 
                    stroke="#fff" 
                    strokeWidth={1.5 / position.zoom}
                    onMouseEnter={() => setTooltipContent(`Agent: ${agent.hostname || agent.agent_id}`)}
                    onMouseLeave={() => setTooltipContent("")}
                    onClick={() => navigate(`/agents/${agent.agent_id}`)}
                    data-tooltip-id="map-tooltip"
                    style={{ cursor: "pointer", filter: `drop-shadow(0px 0px ${4 / position.zoom}px var(--vz-success))` }}
                  />
                </Marker>
              );
            }
            return null;
          })}
        </ZoomableGroup>
      </ComposableMap>
      <Tooltip id="map-tooltip" place="top">{tooltipContent}</Tooltip>
    </div>
  );
};
