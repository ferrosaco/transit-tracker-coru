from __future__ import annotations

import math
from dataclasses import dataclass

from .network import Network, direction_for_bus, global_id, headsign_for_route, local_id


@dataclass(frozen=True)
class RouteStopPair:
    route_id: str
    stop_id: str
    line_id: int
    stop_local: int
    offset: int


@dataclass(frozen=True)
class LiveBus:
    line_id: int
    vehicle: str
    minutes: int
    imminent: bool
    last_stop_id: int | None


def parse_route_stop_pairs(raw: str) -> list[RouteStopPair]:
    pairs: list[RouteStopPair] = []
    for chunk in raw.split(";"):
        chunk = chunk.strip()
        if not chunk:
            continue
        parts = [part.strip() for part in chunk.split(",")]
        if len(parts) not in (2, 3):
            raise ValueError("cada par debe ser routeId,stopId[,offset]")
        route_id, stop_id = parts[0], parts[1]
        offset = int(parts[2]) if len(parts) == 3 else 0
        if not route_id or not stop_id:
            raise ValueError("cada par debe ser routeId,stopId[,offset]")
        pairs.append(
            RouteStopPair(
                route_id=route_id,
                stop_id=stop_id,
                line_id=local_id(route_id),
                stop_local=local_id(stop_id),
                offset=offset,
            )
        )
    return pairs


def parse_buses(payload: dict) -> list[LiveBus]:
    if payload.get("resultado") != "OK":
        raise ValueError(payload.get("resultado") or "respuesta de iTranvías no válida")
    buses = (payload.get("buses") or {}).get("lineas") or []
    live: list[LiveBus] = []
    for line in buses:
        line_id = int(line["linea"])
        for bus in line.get("buses") or []:
            minutes, imminent = parse_minutes(str(bus.get("tiempo", "")))
            if minutes is None:
                continue
            last = bus.get("ult_parada")
            live.append(
                LiveBus(
                    line_id=line_id,
                    vehicle=str(bus.get("bus")),
                    minutes=minutes,
                    imminent=imminent,
                    last_stop_id=int(last) if last not in (None, "") else None,
                )
            )
    return live


def parse_minutes(value: str) -> tuple[int | None, bool]:
    text = value.strip()
    if not text:
        return None, False
    if text.startswith("<"):
        return 0, True
    try:
        return int(text), False
    except ValueError:
        return None, False


def eta_timestamp(now: int, bus: LiveBus, offset: int) -> int | None:
    """Unix time que debe pintar el panel, o None si el margen de camino ya se pasó.

    iTranvías da minutos enteros. El filtro usa ese minuto exacto (un bus a 5 min
    con 5 min de camino no se muestra). El timestamp publicado suma 59 s para que
    el firmware siga dibujando ese mismo minuto durante casi un minuto.
    """
    imminent = bus.imminent or bus.minutes <= 0
    raw = now + (20 if imminent else bus.minutes * 60)
    adjusted = raw + offset
    if adjusted <= now:
        return None
    if imminent:
        return adjusted
    return adjusted + 59


def bbox_area_km2(west: float, south: float, east: float, north: float) -> float:
    mid = math.radians((south + north) / 2)
    width_km = abs(east - west) * 111.32 * math.cos(mid)
    height_km = abs(north - south) * 110.57
    return width_km * height_km


def build_trips(
    network: Network,
    pairs: list[RouteStopPair],
    buses_by_stop: dict[int, list[LiveBus]],
    now: int,
    *,
    limit: int,
    list_mode: str | None,
) -> list[dict]:
    trips: list[dict] = []
    for pair in pairs:
        line = network.lines.get(pair.line_id)
        stop = network.stops.get(pair.stop_local)
        if line is None or stop is None:
            continue
        for bus in buses_by_stop.get(pair.stop_local, []):
            if bus.line_id != pair.line_id:
                continue
            when = eta_timestamp(now, bus, pair.offset)
            if when is None:
                continue
            direction = direction_for_bus(line, pair.stop_local, bus.last_stop_id)
            direction_id = str(direction) if direction is not None else None
            headsign = (
                headsign_for_route(line, direction, network.stops)
                if direction is not None
                else (line.destination or line.origin or line.name)
            )
            trips.append(
                {
                    "tripId": global_id(bus.line_id) + f":{bus.vehicle}:{pair.stop_local}",
                    "stopId": pair.stop_id,
                    "routeId": pair.route_id,
                    "routeName": line.name,
                    "routeColor": line.color,
                    "stopName": stop.name,
                    "headsign": headsign,
                    "directionId": direction_id,
                    "arrivalTime": when,
                    "departureTime": when,
                    "vehicle": bus.vehicle,
                    "isRealtime": True,
                    "_sort": when,
                }
            )

    trips.sort(key=lambda trip: trip["_sort"])

    if list_mode == "nextPerRoute":
        seen: set[str] = set()
        filtered = []
        for trip in trips:
            key = f"{trip['routeId']}-{trip['directionId']}"
            if key in seen:
                continue
            seen.add(key)
            filtered.append(trip)
        trips = filtered

    trimmed = trips[:limit]
    for trip in trimmed:
        trip.pop("_sort", None)
    return trimmed
