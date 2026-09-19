import { useEffect, useState } from "react";

const API_URL = `http://${window.location.hostname}:8000`;

function App() {
  const [device, setDevice] = useState(null);
  const [loading, setLoading] = useState(true);
  const [message, setMessage] = useState("");

  async function fetchStatus() {
    try {
      const response = await fetch(`${API_URL}/device/status`, {
        cache: "no-store",
      });

      if (!response.ok) {
        throw new Error("Backend request failed");
      }

      const data = await response.json();

      setDevice(data);
      setLoading(false);

    } catch (error) {

      console.error(error);

      setDevice(null);
      setLoading(false);
    }
  }


  async function simulateFall() {

    try {

      await fetch(`${API_URL}/test/fall`, {
        method: "POST",
      });

      setMessage("Test fall alert sent!");

      await fetchStatus();

    } catch (error) {

      console.error(error);

      setMessage("Could not contact backend.");
    }
  }


  async function clearAlert() {

    try {

      await fetch(`${API_URL}/device/clear-alert`, {
        method: "POST",
      });

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


  const fallDetected = device?.fall_detected === true;
  const connected = device?.connected === true;


  return (
    <div className="app">

      {/* HEADER */}

      <header className="topbar">

        <div>
          <h1>SmartElderlyCare</h1>
          <p>Caregiver Monitoring Dashboard</p>
        </div>

        <div
          className={
            connected
              ? "connection connected"
              : "connection disconnected"
          }
        >
          <span className="status-dot"></span>

          {connected
            ? "Device Connected"
            : "Device Offline"}
        </div>

      </header>


      {/* EMERGENCY ALERT */}

      {fallDetected && (

        <section className="emergency-alert">

          <div className="alert-icon">
            🚨
          </div>

          <div className="alert-content">

            <h2>FALL DETECTED</h2>

            <p>
              A possible fall has been detected.
              The elderly person may need assistance.
            </p>

            {device.last_fall && (
              <p className="alert-time">
                Detected: {new Date(device.last_fall).toLocaleString()}
              </p>
            )}

          </div>

          <button
            className="clear-button"
            onClick={clearAlert}
          >
            ACKNOWLEDGE
          </button>

        </section>

      )}


      {/* MESSAGE */}

      {message && (

        <div className="message">
          {message}
        </div>

      )}


      {/* MAIN DASHBOARD */}

      <main className="dashboard">

        {/* PERSON STATUS */}

        <section className="card person-card">

          <div className="card-header">

            <div>
              <p className="eyebrow">
                ELDERLY PERSON
              </p>

              <h2>
                Monitoring Status
              </h2>
            </div>

            <div
              className={
                fallDetected
                  ? "big-status danger"
                  : "big-status safe"
              }
            >
              {fallDetected
                ? "🚨 ALERT"
                : "🟢 SAFE"}
            </div>

          </div>


          <div className="person-info">

            <div>
              <span>Device ID</span>
              <strong>
                {device?.device_id}
              </strong>
            </div>

            <div>
              <span>Fall Alerts</span>
              <strong>
                {device?.fall_count ?? 0}
              </strong>
            </div>

            <div>
              <span>Last Update</span>
              <strong>
                {device?.last_update
                  ? new Date(
                      device.last_update
                    ).toLocaleTimeString()
                  : "—"}
              </strong>
            </div>

          </div>

        </section>


        {/* MOTION */}

        <section className="card">

          <div className="card-header">

            <div>
              <p className="eyebrow">
                MPU6050
              </p>

              <h2>
                Motion Data
              </h2>
            </div>

            <div className="sensor-badge">
              LIVE
            </div>

          </div>


          <div className="sensor-grid">

            <div className="sensor-value">
              <span>Acceleration X</span>
              <strong>
                {device?.accel_x?.toFixed(2)} g
              </strong>
            </div>

            <div className="sensor-value">
              <span>Acceleration Y</span>
              <strong>
                {device?.accel_y?.toFixed(2)} g
              </strong>
            </div>

            <div className="sensor-value">
              <span>Acceleration Z</span>
              <strong>
                {device?.accel_z?.toFixed(2)} g
              </strong>
            </div>

            <div className="sensor-value highlight">
              <span>Total Acceleration</span>
              <strong>
                {device?.total_acceleration?.toFixed(2)} g
              </strong>
            </div>

          </div>

        </section>


        {/* GYROSCOPE */}

        <section className="card">

          <div className="card-header">

            <div>
              <p className="eyebrow">
                MPU6050
              </p>

              <h2>
                Gyroscope
              </h2>
            </div>

          </div>


          <div className="sensor-grid">

            <div className="sensor-value">
              <span>Gyro X</span>
              <strong>
                {device?.gyro_x?.toFixed(1)} °/s
              </strong>
            </div>

            <div className="sensor-value">
              <span>Gyro Y</span>
              <strong>
                {device?.gyro_y?.toFixed(1)} °/s
              </strong>
            </div>

            <div className="sensor-value">
              <span>Gyro Z</span>
              <strong>
                {device?.gyro_z?.toFixed(1)} °/s
              </strong>
            </div>

          </div>

        </section>


        {/* TEST CONTROLS */}

        <section className="card test-card">

          <p className="eyebrow">
            DEVELOPMENT TESTING
          </p>

          <h2>
            Emergency Alert Test
          </h2>

          <p>
            Use this button to test the caregiver
            alert without physically dropping or
            throwing the sensor.
          </p>

          <button
            className="test-button"
            onClick={simulateFall}
          >
            🚨 TEST FALL ALERT
          </button>

        </section>

      </main>


      <footer>

        <p>
          SmartElderlyCare • IoT Fall Detection
        </p>

        <p>
          SVM classification will be added later.
        </p>

      </footer>

    </div>
  );
}

export default App;