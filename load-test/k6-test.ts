import http from "k6/http";
import { check, sleep } from "k6";
import { Rate, Trend, Counter } from "k6/metrics";
import { Options } from "k6/options";

const errorRate = new Rate("sensor_error_rate");
const requestDuration = new Trend("sensor_request_duration", true);
const totalRequests = new Counter("sensor_total_requests");

export const options: Options = {
  stages: [
    { duration: "30s", target: 10 },
    { duration: "60s", target: 10 },
    { duration: "30s", target: 50 },
    { duration: "60s", target: 50 },
    { duration: "30s", target: 0 },
  ],

  thresholds: {
    http_req_duration: ["p(95)<500"],
    sensor_error_rate: ["rate<0.01"],
    sensor_request_duration: ["med<200"],
  },
};

type SensorType =
  | "temperatura"
  | "umidade"
  | "presença"
  | "vibração"
  | "luminosidade"
  | "nível_reservatório";

type ReadType = "analog" | "discrete";

interface SensorMessage {
  idSensor: string;
  timestamp: string;
  sensorType: SensorType;
  readType: ReadType;
  value: number;
}

const SENSOR_PROFILES: Array<{
  idSensor: string;
  sensorType: SensorType;
  readType: ReadType;
  valueMin: number;
  valueMax: number;
}> = [
  { idSensor: "SN-TH-001", sensorType: "temperatura",        readType: "analog",   valueMin: 15,  valueMax: 45  },
  { idSensor: "SN-TH-002", sensorType: "temperatura",        readType: "analog",   valueMin: 15,  valueMax: 45  },
  { idSensor: "SN-TH-003", sensorType: "umidade",            readType: "analog",   valueMin: 20,  valueMax: 95  },
  { idSensor: "SN-PIR-001", sensorType: "presença",          readType: "discrete", valueMin: 0,   valueMax: 1   },
  { idSensor: "SN-VIB-001", sensorType: "vibração",          readType: "analog",   valueMin: 0,   valueMax: 10  },
  { idSensor: "SN-LUX-001", sensorType: "luminosidade",      readType: "analog",   valueMin: 0,   valueMax: 1000},
  { idSensor: "SN-NIV-001", sensorType: "nível_reservatório", readType: "analog",  valueMin: 0,   valueMax: 100 },
];

function randFloat(min: number, max: number, decimals = 2): number {
  const raw = Math.random() * (max - min) + min;
  return parseFloat(raw.toFixed(decimals));
}

function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)];
}

function nowISO(): string {
  return new Date().toISOString();
}

function generatePayload(): SensorMessage {
  const profile = pick(SENSOR_PROFILES);
  return {
    idSensor: profile.idSensor,
    timestamp: nowISO(),
    sensorType: profile.sensorType,
    readType: profile.readType,
    value:
      profile.readType === "discrete"
        ? randFloat(profile.valueMin, profile.valueMax, 0)
        : randFloat(profile.valueMin, profile.valueMax),
  };
}

const BASE_URL: string = __ENV.BASE_URL ?? "http://go_backend:8080";
const SENSOR_ENDPOINT = `${BASE_URL}/sensorData`;

const HEADERS = {
  "Content-Type": "application/json",
  Accept: "application/json",
};

export default function (): void {
  const payload = generatePayload();
  const body = JSON.stringify(payload);

  const res = http.post(SENSOR_ENDPOINT, body, { headers: HEADERS });

  requestDuration.add(res.timings.duration);
  totalRequests.add(1);

  const success = check(res, {
    "status 2xx": (r) => r.status >= 200 && r.status < 300,
    "response time < 500ms": (r) => r.timings.duration < 500,
    "body não vazio": (r) => (r.body as string)?.length > 0,
  });

  errorRate.add(!success);

  if (!success) {
    console.warn(
      `[FALHA] ${payload.idSensor} | status=${res.status} | dur=${res.timings.duration.toFixed(0)}ms | body=${res.body}`
    );
  }

  sleep(randFloat(0.1, 0.5, 3));
}