import { useState } from "react";
import useConfigStore from "@/store/config";

export function useModeData() {
  const { deviceInfo, updateDeviceInfo } = useConfigStore();
  const [mode, setMode] = useState<"mqtt" | "http">("mqtt");

  const switchMode = (newMode: "mqtt" | "http") => {
    setMode(newMode);
    // opcional: guardar en configStore o llamar API para aplicar el cambio
    updateDeviceInfo({ ...deviceInfo, mode: newMode });
  };

  return { mode, switchMode };
}
