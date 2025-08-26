import { useEffect } from "react";
import useConfigStore from "@/store/config";

const Mode = () => {
  const { mode, setMode, loadMode } = useConfigStore();

  useEffect(() => {
    // cargamos el modo guardado al iniciar la vista
    loadMode();
  }, [loadMode]);

  return (
    <div style={{ padding: 20 }}>
      <h2>Seleccioná el modo</h2>
      <div style={{ marginTop: 10 }}>
        <button
          style={{
            marginRight: 10,
            backgroundColor: mode === "mqtt" ? "#4caf50" : "#ccc",
            color: "#fff",
            padding: "8px 16px",
            border: "none",
            borderRadius: 4,
            cursor: "pointer",
          }}
          onClick={() => setMode("mqtt")}
        >
          MQTT
        </button>
        <button
          style={{
            backgroundColor: mode === "http" ? "#4caf50" : "#ccc",
            color: "#fff",
            padding: "8px 16px",
            border: "none",
            borderRadius: 4,
            cursor: "pointer",
          }}
          onClick={() => setMode("http")}
        >
          HTTP
        </button>
      </div>
      <p style={{ marginTop: 20 }}>Modo actual: {mode.toUpperCase()}</p>
    </div>
  );
};

export default Mode;
