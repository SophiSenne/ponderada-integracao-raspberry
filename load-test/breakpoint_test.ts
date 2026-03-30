import http from "k6/http";
import { check, sleep } from "k6";
import { Rate, Trend, Counter } from "k6/metrics";
import { Options } from "k6/options";

const errorRate       = new Rate("sensor_error_rate");
const requestDuration = new Trend("sensor_request_duration", true);
const totalRequests   = new Counter("sensor_total_requests");
const rabbitErrors    = new Counter("rabbitmq_publish_errors");
const timeoutErrors   = new Counter("timeout_errors");

export const options: Options = {
  stages: [
    { duration: "0.3m",  target: 50   },  
    { duration: "1m",  target: 500  }, 
    { duration: "1m",  target: 1000 }, 
    { duration: "1m",  target: 2000 },  
    { duration: "1m",  target: 3000 },  
    { duration: "1m",  target: 5000 }, 
    { duration: "2m",  target: 5000 }, 
    { duration: "1m",  target: 0   },  
  ],

  thresholds: {
    http_req_duration:       ["p(95)<5000"],
    sensor_error_rate:       ["rate<0.20"],
    sensor_request_duration: ["med<2000"],
  },
};

type SensorType = "temperatura" | "umidade" | "presença" | "vibração" | "luminosidade" | "nível_reservatório";
type ReadType   = "analog" | "discrete";

interface SensorMessage {
  idSensor:   string;
  timestamp:  string;
  sensorType: SensorType;
  readType:   ReadType;
  value:      number;
}

const SENSOR_PROFILES = [
  { idSensor: "SN-TH-001",  sensorType: "temperatura"        as SensorType, readType: "analog"   as ReadType, valueMin: 15, valueMax: 45   },
  { idSensor: "SN-TH-002",  sensorType: "temperatura"        as SensorType, readType: "analog"   as ReadType, valueMin: 15, valueMax: 45   },
  { idSensor: "SN-TH-003",  sensorType: "umidade"            as SensorType, readType: "analog"   as ReadType, valueMin: 20, valueMax: 95   },
  { idSensor: "SN-PIR-001", sensorType: "presença"           as SensorType, readType: "discrete" as ReadType, valueMin: 0,  valueMax: 1    },
  { idSensor: "SN-VIB-001", sensorType: "vibração"           as SensorType, readType: "analog"   as ReadType, valueMin: 0,  valueMax: 10   },
  { idSensor: "SN-LUX-001", sensorType: "luminosidade"       as SensorType, readType: "analog"   as ReadType, valueMin: 0,  valueMax: 1000 },
  { idSensor: "SN-NIV-001", sensorType: "nível_reservatório" as SensorType, readType: "analog"   as ReadType, valueMin: 0,  valueMax: 100  },
];

function randFloat(min: number, max: number, decimals = 2): number {
  return parseFloat((Math.random() * (max - min) + min).toFixed(decimals));
}
function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)];
}
function generatePayload(): SensorMessage {
  const p = pick(SENSOR_PROFILES);
  return {
    idSensor:   p.idSensor,
    timestamp:  new Date().toISOString(),
    sensorType: p.sensorType,
    readType:   p.readType,
    value:      p.readType === "discrete"
                  ? randFloat(p.valueMin, p.valueMax, 0)
                  : randFloat(p.valueMin, p.valueMax),
  };
}

const BASE_URL        = (__ENV.BASE_URL ?? "http://go_backend:8080") as string;
const SENSOR_ENDPOINT = `${BASE_URL}/sensorData`;
const HEADERS         = { "Content-Type": "application/json", Accept: "application/json" };

export default function (): void {
  const payload = generatePayload();
  const res = http.post(SENSOR_ENDPOINT, JSON.stringify(payload), {
    headers: HEADERS,
    timeout: "10s",
  });

  requestDuration.add(res.timings.duration);
  totalRequests.add(1);

  if (res.status === 500) rabbitErrors.add(1);
  if (res.status === 0)   timeoutErrors.add(1);

  const success = check(res, {
    "status 2xx":         (r) => r.status >= 200 && r.status < 300,
    "response time < 5s": (r) => r.timings.duration < 5000,
    "body não vazio":     (r) => (r.body as string)?.length > 0,
  });

  errorRate.add(!success);

  if (!success) {
    console.warn(
      `[FALHA] ${payload.idSensor} | status=${res.status} | dur=${res.timings.duration.toFixed(0)}ms | body=${res.body}`
    );
  }

  sleep(randFloat(0.05, 0.15, 3));
}