from __future__ import annotations

import unicodedata
from dataclasses import dataclass

from . import FEED_CODE


@dataclass(frozen=True)
class Line:
    id: int
    name: str
    origin: str
    destination: str
    color: str | None
    routes: tuple[tuple[int, ...], ...]


@dataclass(frozen=True)
class Stop:
    id: int
    name: str
    lon: float
    lat: float
    lines: tuple[int, ...]

    @property
    def global_id(self) -> str:
        return global_id(self.id)


@dataclass(frozen=True)
class Network:
    lines: dict[int, Line]
    stops: dict[int, Stop]
    synced_at: str | None

    def bounds(self) -> list[float]:
        lons = [stop.lon for stop in self.stops.values()]
        lats = [stop.lat for stop in self.stops.values()]
        return [min(lons), min(lats), max(lons), max(lats)]

    def service_area(self) -> dict:
        west, south, east, north = self.bounds()
        ring = [
            [west, south],
            [east, south],
            [east, north],
            [west, north],
            [west, south],
        ]
        return {
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "properties": {"code": FEED_CODE, "name": "Tranvías de A Coruña"},
                    "geometry": {"type": "Polygon", "coordinates": [ring]},
                }
            ],
        }

    def stops_within(self, west: float, south: float, east: float, north: float) -> list[Stop]:
        return [
            stop
            for stop in self.stops.values()
            if west <= stop.lon <= east and south <= stop.lat <= north
        ]

    def search_stops(self, query: str, limit: int = 20) -> list[Stop]:
        needle = fold(query.strip())
        if len(needle) < 2 and not needle.isdigit():
            return []
        scored: list[tuple[int, str, Stop]] = []
        for stop in self.stops.values():
            name = fold(stop.name)
            code = str(stop.id)
            if needle == code:
                rank = 0
            elif code.startswith(needle) and needle.isdigit():
                rank = 1
            elif name.startswith(needle):
                rank = 2
            elif needle in name:
                rank = 3
            else:
                continue
            scored.append((rank, name, stop))
        scored.sort(key=lambda item: (item[0], item[1]))
        return [stop for _, _, stop in scored[:limit]]


def fold(text: str) -> str:
    decomposed = unicodedata.normalize("NFD", text)
    stripped = "".join(char for char in decomposed if unicodedata.category(char) != "Mn")
    return stripped.casefold()


def global_id(local_id: int) -> str:
    return f"{FEED_CODE}:{local_id}"


def local_id(value: str) -> int:
    text = value.strip()
    if ":" in text:
        feed, local = text.split(":", 1)
        if feed != FEED_CODE:
            raise ValueError(f"feed desconocido: {feed}")
        text = local
    return int(text)


def parse_network(payload: dict) -> Network:
    update = payload["iTranvias"]["actualizacion"]
    lines: dict[int, Line] = {}
    for raw in update.get("lineas", []):
        line_id = int(raw["id"])
        color = raw.get("color") or None
        if isinstance(color, str):
            color = color.removeprefix("#") or None
        routes = tuple(
            tuple(int(stop_id) for stop_id in route.get("paradas", []))
            for route in raw.get("rutas", [])
        )
        lines[line_id] = Line(
            id=line_id,
            name=str(raw.get("lin_comer") or line_id),
            origin=str(raw.get("nombre_orig") or ""),
            destination=str(raw.get("nombre_dest") or ""),
            color=color,
            routes=routes,
        )

    stops: dict[int, Stop] = {}
    for raw in update.get("paradas", []):
        stop_id = int(raw["id"])
        stops[stop_id] = Stop(
            id=stop_id,
            name=str(raw.get("nombre") or stop_id),
            lon=float(raw["posx"]),
            lat=float(raw["posy"]),
            lines=tuple(int(line_id) for line_id in raw.get("enlaces", [])),
        )

    synced = update.get("fecha")
    return Network(lines=lines, stops=stops, synced_at=str(synced) if synced else None)


def headsign_for_route(line: Line, route_index: int, stops: dict[int, Stop]) -> str:
    if route_index == 0 and line.destination:
        return line.destination
    if route_index == 1 and line.origin:
        return line.origin
    route = line.routes[route_index]
    if route:
        last = stops.get(route[-1])
        if last is not None:
            return last.name
    return line.destination or line.origin or line.name


def headsigns_at_stop(line: Line, stop_id: int, stops: dict[int, Stop]) -> list[str]:
    seen: list[str] = []
    for index, route in enumerate(line.routes):
        if stop_id not in route:
            continue
        label = headsign_for_route(line, index, stops)
        if label and label not in seen:
            seen.append(label)
    return seen


def direction_for_bus(line: Line, stop_id: int, last_stop_id: int | None) -> int | None:
    """Elige el recorrido en el que el bus todavía no ha pasado la parada."""
    best: tuple[int, int] | None = None
    for index, route in enumerate(line.routes):
        if stop_id not in route:
            continue
        stop_index = route.index(stop_id)
        if last_stop_id is None or last_stop_id not in route:
            continue
        last_index = route.index(last_stop_id)
        if last_index <= stop_index:
            distance = stop_index - last_index
            if best is None or distance < best[0]:
                best = (distance, index)
    if best is not None:
        return best[1]

    present = [index for index, route in enumerate(line.routes) if stop_id in route]
    if len(present) == 1:
        return present[0]
    return present[0] if present else None
