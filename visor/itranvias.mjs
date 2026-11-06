const QUERY = "https://itranvias.com/queryitr_v3.php";
const STATIC_DATO = "20160101T000000_es_0_20160101T000000";
const FEED = "coruna";

let networkPromise = null;
const arrivalCache = new Map();

export function resetNetwork() {
  networkPromise = null;
}

export async function loadNetwork() {
  if (!networkPromise) {
    networkPromise = fetchStatic().catch((error) => {
      networkPromise = null;
      throw error;
    });
  }
  return networkPromise;
}

async function fetchStatic() {
  const payload = await getJson({ func: "7", dato: STATIC_DATO });
  return parseNetwork(payload);
}

export async function searchStops(query) {
  const network = await loadNetwork();
  return network.search(query).map(stopJson);
}

export async function routesForStop(stopId) {
  const network = await loadNetwork();
  const stop = network.stops.get(localId(stopId));
  if (!stop) return null;
  return stop.lines
    .map((lineId) => network.lines.get(lineId))
    .filter(Boolean)
    .map((line) => ({
      routeId: globalId(line.id),
      name: line.name,
      color: line.color,
      headsigns: headsignsAtStop(line, stop.id, network.stops),
    }));
}

export async function scheduleFor(pairsRaw, limit = 8) {
  const network = await loadNetwork();
  const pairs = parsePairs(pairsRaw).slice(0, 25);
  const byStop = new Map();
  for (const pair of pairs) {
    if (byStop.has(pair.stopLocal)) continue;
    byStop.set(pair.stopLocal, await arrivalsFor(pair.stopLocal));
  }
  const now = Math.floor(Date.now() / 1000);
  const trips = [];
  for (const pair of pairs) {
    const line = network.lines.get(pair.lineId);
    const stop = network.stops.get(pair.stopLocal);
    if (!line || !stop) continue;
    for (const bus of byStop.get(pair.stopLocal) || []) {
      if (bus.lineId !== pair.lineId) continue;
      const when = eta(now, bus, pair.offset);
      if (when == null) continue;
      const direction = directionForBus(line, pair.stopLocal, bus.lastStopId);
      trips.push({
        tripId: `${globalId(bus.lineId)}:${bus.vehicle}:${pair.stopLocal}`,
        stopId: pair.stopId,
        routeId: pair.routeId,
        routeName: line.name,
        routeColor: line.color,
        stopName: stop.name,
        headsign:
          direction == null
            ? line.destination || line.origin || line.name
            : headsignForRoute(line, direction, network.stops),
        directionId: direction == null ? null : String(direction),
        arrivalTime: when,
        departureTime: when,
        vehicle: bus.vehicle,
        isRealtime: true,
      });
    }
  }
  trips.sort((a, b) => a.arrivalTime - b.arrivalTime);
  return trips.slice(0, limit);
}

async function arrivalsFor(stopId) {
  const cached = arrivalCache.get(stopId);
  if (cached && Date.now() - cached.at < 10000) return cached.buses;
  const payload = await getJson({ func: "0", dato: String(stopId) });
  const buses = parseBuses(payload);
  arrivalCache.set(stopId, { at: Date.now(), buses });
  return buses;
}

async function getJson(params) {
  const url = new URL(QUERY);
  for (const [key, value] of Object.entries(params)) url.searchParams.set(key, value);
  const response = await fetch(url, {
    headers: { accept: "application/json", "user-agent": "TransitTrackerCoruna/1.0" },
  });
  if (!response.ok) throw new Error("iTranvías respondió " + response.status);
  const payload = await response.json();
  if (!payload || typeof payload !== "object") throw new Error("respuesta no válida");
  return payload;
}

function parseNetwork(payload) {
  const update = payload.iTranvias.actualizacion;
  const lines = new Map();
  for (const raw of update.lineas || []) {
    lines.set(Number(raw.id), {
      id: Number(raw.id),
      name: String(raw.lin_comer || raw.id),
      origin: String(raw.nombre_orig || ""),
      destination: String(raw.nombre_dest || ""),
      color: raw.color ? String(raw.color).replace(/^#/, "") : null,
      routes: (raw.rutas || []).map((route) => (route.paradas || []).map(Number)),
    });
  }
  const stops = new Map();
  for (const raw of update.paradas || []) {
    stops.set(Number(raw.id), {
      id: Number(raw.id),
      name: String(raw.nombre || raw.id),
      lon: Number(raw.posx),
      lat: Number(raw.posy),
      lines: (raw.enlaces || []).map(Number),
    });
  }
  return { lines, stops, search: (query) => search(stops, query) };
}

function search(stops, query) {
  const needle = fold(query.trim());
  if (needle.length < 2 && !/^\d+$/.test(needle)) return [];
  const scored = [];
  for (const stop of stops.values()) {
    const name = fold(stop.name);
    const code = String(stop.id);
    let rank = 4;
    if (needle === code) rank = 0;
    else if (/^\d+$/.test(needle) && code.startsWith(needle)) rank = 1;
    else if (name.startsWith(needle)) rank = 2;
    else if (name.includes(needle)) rank = 3;
    else continue;
    scored.push([rank, name, stop]);
  }
  scored.sort((a, b) => a[0] - b[0] || a[1].localeCompare(b[1], "es"));
  return scored.slice(0, 20).map((item) => item[2]);
}

function parseBuses(payload) {
  if (payload.resultado !== "OK") throw new Error("iTranvías no devolvió llegadas");
  const live = [];
  for (const line of payload.buses?.lineas || []) {
    for (const bus of line.buses || []) {
      const parsed = parseMinutes(String(bus.tiempo ?? ""));
      if (!parsed) continue;
      live.push({
        lineId: Number(line.linea),
        vehicle: String(bus.bus),
        minutes: parsed.minutes,
        imminent: parsed.imminent,
        lastStopId: bus.ult_parada == null || bus.ult_parada === "" ? null : Number(bus.ult_parada),
      });
    }
  }
  return live;
}

function parseMinutes(value) {
  const text = value.trim();
  if (!text) return null;
  if (text.startsWith("<")) return { minutes: 0, imminent: true };
  const minutes = Number(text);
  if (!Number.isInteger(minutes)) return null;
  return { minutes, imminent: false };
}

function eta(now, bus, offset) {
  const imminent = bus.imminent || bus.minutes <= 0;
  const raw = now + (imminent ? 20 : bus.minutes * 60);
  const adjusted = raw + offset;
  if (adjusted <= now) return null;
  return imminent ? adjusted : adjusted + 59;
}

function parsePairs(raw) {
  return String(raw || "")
    .split(";")
    .map((chunk) => chunk.trim())
    .filter(Boolean)
    .map((chunk) => {
      const parts = chunk.split(",").map((part) => part.trim());
      if (parts.length < 2 || parts.length > 3) throw new Error("par de línea y parada no válido");
      return {
        routeId: parts[0],
        stopId: parts[1],
        lineId: localId(parts[0]),
        stopLocal: localId(parts[1]),
        offset: parts[2] ? Number(parts[2]) : 0,
      };
    });
}

function directionForBus(line, stopId, lastStopId) {
  let best = null;
  line.routes.forEach((route, index) => {
    const stopIndex = route.indexOf(stopId);
    if (stopIndex < 0 || lastStopId == null) return;
    const lastIndex = route.indexOf(lastStopId);
    if (lastIndex < 0 || lastIndex > stopIndex) return;
    const distance = stopIndex - lastIndex;
    if (!best || distance < best.distance) best = { distance, index };
  });
  if (best) return best.index;
  const present = line.routes.map((route, index) => (route.includes(stopId) ? index : -1)).filter((index) => index >= 0);
  return present[0] ?? null;
}

function headsignForRoute(line, index, stops) {
  if (index === 0 && line.destination) return line.destination;
  if (index === 1 && line.origin) return line.origin;
  const route = line.routes[index] || [];
  const last = stops.get(route[route.length - 1]);
  return last?.name || line.destination || line.origin || line.name;
}

function headsignsAtStop(line, stopId, stops) {
  const seen = [];
  line.routes.forEach((route, index) => {
    if (!route.includes(stopId)) return;
    const label = headsignForRoute(line, index, stops);
    if (label && !seen.includes(label)) seen.push(label);
  });
  return seen;
}

function localId(value) {
  let text = String(value).trim();
  if (text.includes(":")) {
    const [feed, local] = text.split(":");
    if (feed !== FEED) throw new Error("feed desconocido");
    text = local;
  }
  if (!/^\d+$/.test(text)) throw new Error("identificador no válido");
  return Number(text);
}

function globalId(id) {
  return FEED + ":" + id;
}

function stopJson(stop) {
  return {
    stopId: globalId(stop.id),
    stopCode: String(stop.id),
    name: stop.name,
    lat: stop.lat,
    lon: stop.lon,
  };
}

function fold(text) {
  return text.normalize("NFD").replace(/\p{M}/gu, "").toLocaleLowerCase("es");
}
