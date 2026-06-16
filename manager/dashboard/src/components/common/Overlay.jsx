const Overlay = ({ children, height = 420, type = 'coming-soon', compact = false }) => {
  const computedHeight = typeof height === 'number' ? `${height}px` : height;

  const messages = {
    'coming-soon': 'Coming Soon',
    'upgrade': 'Upgrade',
  };

  const message = messages[type] || 'Unavailable';

  return (
    <div
      className="position-relative"
      style={{ height: computedHeight, width: '100%' }}
    >
      {/* Content container with pointer events blocked */}
      <div style={{ pointerEvents: 'none', filter: 'blur(2px)', opacity: 0.4 }}>
        {children}
      </div>

      {/* Overlay with transparent background */}
      <div
        className="position-absolute top-0 start-0 w-100 h-100 d-flex align-items-center justify-content-center rounded"
        style={{
          zIndex: 10,
          pointerEvents: 'auto',
          padding: compact ? '4px' : '10px',
          cursor: 'not-allowed',
          backgroundColor: 'transparent',
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <h2
          className="fw-semibold m-0"
          style={{
            fontSize: compact ? '0.75rem' : '1rem',
            color: 'white',
            backgroundColor: 'rgba(255, 107, 53, 0.9)', // Orange background
            padding: compact ? '4px 8px' : '10px 20px',
            borderRadius: '7px',
            textAlign: 'center',
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
            width: 'fit-content',
            boxShadow: '0 4px 12px rgba(255, 107, 53, 0.4)', // Orange shadow
            border: '1px solid rgba(255, 107, 53, 1)', // Orange border
          }}
        >
          <i
            className="ri-lock-line"
            style={{
              fontSize: compact ? '1rem' : '1.5rem',
              color: 'white',
            }}
          />
          {!compact && message}
        </h2>
      </div>
    </div>
  );
};

export default Overlay;