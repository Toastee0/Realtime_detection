import { supervisorRequest } from "@/utils/request";

export type ModeType = "mqtt" | "http";

// obtener modo actual
export const getModeApi = async () =>
  supervisorRequest<{ mode: ModeType }>({
    url: "api/mode/get",
    method: "get",
  });

// cambiar modo
export const setModeApi = async (mode: ModeType) =>
  supervisorRequest({
    url: "api/mode/set",
    method: "post",
    data: { mode },
  });
