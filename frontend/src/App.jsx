import { useEffect, useRef, useState } from "react";

// In `npm run dev`, Vite proxies /device and /test to the FastAPI server
// so the browser never has to talk to :8000 (avoids CORS / mixed-host misses).
const API_URL = import.meta.env.VITE_API_URL ?? (
  import.meta.env.DEV ? "" : `http://${window.location.hostname}:8001`
);

function isFallFlag(value) {
  return value === true || value === "true" || value === 1 || value === "1";
}

function App() {
  const [device, setDevice] = useState(null);
  const [loading, setLoading] = useState(true);
  const [message, setMessage] = useState("");
  const [alertLatched, setAlertLatched] = useState(false);

  const statusRequestId = useRef(0);
  const seenFallCount = useRef(null);

  async function fetchStatus() {
    const requestId = ++statusRequestId.current;

    try {
      const response = await fetch(`${API_URL}/device/status`, {
        cache: "no-store",
      });

      if (!response.ok) {
        throw new Error("Backend request failed");
      }

      const data = await response.json();

      // Ignore stale overlapping polls — an older "no fall" response
      // must not wipe a newer "fall detected" one.
      if (requestId !== statusRequestId.current) {
        return;
      }

      setDevice(data);
      setLoading(false);

      const incomingCount =
        typeof data.fall_count === "number" ? data.fall_count : 0;
      const isFall = isFallFlag(data.fall_detected);

      if (seenFallCount.current === null) {
        seenFallCount.current = incomingCount;
        if (isFall) {
          setAlertLatched(true);
        }
        return;
      }

      const newFallEvent = incomingCount > seenFallCount.current;
      seenFallCount.current = Math.max(seenFallCount.current, incomingCount);

      if (isFall || newFallEvent) {
        setAlertLatched(true);
      }
    } catch (error) {
      console.error(error);
      setLoading(false);
      // Keep the last known device + alert. A dropped poll must not
      // hide a fall that was already shown.
    }
  }

  async function simulateFall() {
    try {
      const response = await fetch(`${API_URL}/test/fall`, {
        method: "POST",
      });

      if (!response.ok) {
        throw new Error("Test fall request failed");
      }

      setAlertLatched(true);
      setMessage("Test fall alert sent!");
      await fetchStatus();
    } catch (error) {
      console.error(error);
      setMessage("Could not contact backend.");
    }
  }

  async function clearAlert() {
    // Drop any in-flight status poll so a stale "fall=true" cannot
    // re-open the banner after Acknowledge.
    statusRequestId.current += 1;

    try {
      const response = await fetch(`${API_URL}/device/clear-alert`, {
        method: "POST",
      });

      if (!response.ok) {
        throw new Error("Clear alert request failed");
      }

      setAlertLatched(false);
      setMessage("Alert cleared.");
      await fetchStatus();
    } catch (error) {
      console.error(error);
      setMessage("Could not clear alert.");
    }
  }

  useEffect(() => {
    fetchStatus();
    const interval = setInterval(fetchStatus, 1000);
    return () => clearInterval(interval);
  }, []);

  if (loading) {
    return (
      <div className="app loading-screen">
        <h1>SmartElderlyCare</h1>
        <p>Connecting to monitoring system...</p>
      </div>
    );
  }

  const fallDetected = alertLatched || isFallFlag(device?.fall_detected);
  const connected = device?.connected === true;

  return (
    <div className="app">
      <header className="topbar">
        <div>
          <h1>SmartElderlyCare</h1>
          <p>Caregiver Monitoring Dashboard</p>
        </div>

        <div
          className={
            connected ? "connection connected" : "connection disconnected"
          }
        >
          <span className="status-dot"></span>
          {connected ? "Device Connected" : "Device Offline"}
        </div>
      </header>

      {fallDetected && (
        <section className="emergency-alert" role="alert" aria-live="assertive">
          <div className="alert-icon">🚨</div>

          <div className="alert-content">
            <h2>FALL DETECTED</h2>
            <p>
              A possible fall has been detected.
              The elderly person may need assistance.
            </p>
            {device?.last_fall && (
              <p className="alert-time">
                Detected: {new Date(device.last_fall).toLocaleString()}
              </p>
            )}
          </div>

          <button className="clear-button" onClick={clearAlert}>
            ACKNOWLEDGE
          </button>
        </section>
      )}

      {message && <div className="message">{message}</div>}

      <main className="dashboard">
        <section className="card person-card">
          <div className="card-header">
            <div>
              <p className="eyebrow">ELDERLY PERSON</p>
              <h2>Monitoring Status</h2>
            </div>

            <div className={fallDetected ? "big-status danger" : "big-status safe"}>
              {fallDetected ? "🚨 ALERT" : "🟢 SAFE"}
            </div>
          </div>

          <div className="person-info">
            <div>
              <span>Device ID</span>
              <strong>{device?.device_id}</strong>
            </div>

            <div>
              <span>Fall Alerts</span>
              <strong>{device?.fall_count ?? 0}</strong>
            </div>

            <div>
              <span>Last Update</span>
              <strong>
                {device?.last_update
                  ? new Date(device.last_update).toLocaleTimeString()
                  : "—"}
              </strong>
            </div>
          </div>
        </section>

        <section className="card">
          <div className="card-header">
            <div>
              <p className="eyebrow">MPU6050</p>
              <h2>Motion Data</h2>
            </div>
            <div className="sensor-badge">LIVE</div>
          </div>

          <div className="sensor-grid">
            <div className="sensor-value">
              <span>Acceleration X</span>
              <strong>{device?.accel_x?.toFixed(2)} g</strong>
            </div>

            <div className="sensor-value">
              <span>Acceleration Y</span>
              <strong>{device?.accel_y?.toFixed(2)} g</strong>
            </div>

            <div className="sensor-value">
              <span>Acceleration Z</span>
              <strong>{device?.accel_z?.toFixed(2)} g</strong>
            </div>

            <div className="sensor-value highlight">
              <span>Total Acceleration</span>
              <strong>{device?.total_acceleration?.toFixed(2)} g</strong>
            </div>
          </div>
        </section>

        <section className="card">
          <div className="card-header">
            <div>
              <p className="eyebrow">MPU6050</p>
              <h2>Gyroscope</h2>
            </div>
          </div>

          <div className="sensor-grid">
            <div className="sensor-value">
              <span>Gyro X</span>
              <strong>{device?.gyro_x?.toFixed(1)} °/s</strong>
            </div>

            <div className="sensor-value">
              <span>Gyro Y</span>
              <strong>{device?.gyro_y?.toFixed(1)} °/s</strong>
            </div>

            <div className="sensor-value">
              <span>Gyro Z</span>
              <strong>{device?.gyro_z?.toFixed(1)} °/s</strong>
            </div>
          </div>
        </section>

        <section className="card test-card">
          <p className="eyebrow">DEVELOPMENT TESTING</p>
          <h2>Emergency Alert Test</h2>
          <p>
            Use this button to test the caregiver alert without physically
            dropping or throwing the sensor.
          </p>
          <button className="test-button" onClick={simulateFall}>
            🚨 TEST FALL ALERT
          </button>
        </section>
      </main>

      <footer>
        <p>SmartElderlyCare • IoT Fall Detection</p>
        <p>SVM classification will be added later.</p>
      </footer>
    </div>
  );
}

export default App;
