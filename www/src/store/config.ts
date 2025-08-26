import { create } from "zustand";
import { IDeviceInfo } from "@/api/device/device";
import { WifiConnectStatus } from "@/enum/network";
import { DeviceChannleMode, UpdateStatus } from "@/enum";

interface SystemUpdateState {
  status: UpdateStatus;
  visible: boolean;
  channelVisible: boolean;
  updateInfoVisible: boolean;
  percent: number;
  channel: DeviceChannleMode;
  address: string;
  downloadUrl: string;
}

type ConfigStoreType = {
  deviceInfo: Partial<IDeviceInfo>;
  wifiStatus: WifiConnectStatus | undefined;
  systemUpdateState: Partial<SystemUpdateState>;
  mode: "mqtt" | "http";          // <-- agregado
  updateDeviceInfo: (deviceInfo: IDeviceInfo) => void;
  updateWifiStatus: (wifiStatus: WifiConnectStatus) => void;
  setSystemUpdateState: (payload: Partial<SystemUpdateState>) => void;
  setMode: (mode: "mqtt" | "http") => void;   // <-- agregado
  loadMode: () => void;                        // <-- agregado
};

const useConfigStore = create<ConfigStoreType>((set) => ({
  deviceInfo: {},
  wifiStatus: undefined,
  systemUpdateState: {
    status: UpdateStatus.Check,
    visible: false,
    channelVisible: false,
    percent: 0,
    channel: DeviceChannleMode.Official,
    updateInfoVisible: false,
    address: "",
    downloadUrl: "",
  },
  mode: "mqtt",   // valor por defecto
  updateDeviceInfo: (deviceInfo: IDeviceInfo) => set(() => ({ deviceInfo })),
  updateWifiStatus: (wifiStatus: WifiConnectStatus) =>
    set(() => ({ wifiStatus })),
  setSystemUpdateState: (payload) =>
    set((state) => ({
      systemUpdateState: { ...state.systemUpdateState, ...payload },
    })),
  setMode: (mode: "mqtt" | "http") => {
    set({ mode });
    localStorage.setItem("user_mode", mode);
  },
  loadMode: () => {
    const savedMode = localStorage.getItem("user_mode") as "mqtt" | "http" | null;
    if (savedMode) set({ mode: savedMode });
  },
}));

export default useConfigStore;
