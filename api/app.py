"""Web pública para el mapa del configurador.

El panel no la consulta: las llegadas las pide él a iTranvías. Aquí solo viven
las rutas REST que el configurador usa para elegir parada y línea.
"""

from __future__ import annotations

import time

from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, JSONResponse

from itranvias_adapter.client import ItranviasClient
from itranvias_adapter.network import global_id, headsigns_at_stop, local_id
from itranvias_adapter.schedule import bbox_area_km2, parse_route_stop_pairs
from itranvias_adapter.server import MAX_AREA_KM2, MAX_HTTP_LIMIT, MAX_PAIRS, Catalog, _limit, _trips_for

STATIC_TTL_SECONDS = 6 * 60 * 60

HOME_HTML = """<!doctype html>
<html lang="es">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Configurador Coruña</title>
<body style="font-family: system-ui, sans-serif; max-width: 38rem; margin: 3rem auto; padding: 0 1rem; line-height: 1.5">
<p>Hola!! Este link va en el campo "API". A ver si funciona.<br>-Darío :-)</p>
</body>
</html>
"""


def create_public_app(client: ItranviasClient | None = None) -> FastAPI:
    catalog = Catalog(client or ItranviasClient())
    loaded_at = 0.0
    app = FastAPI(title="Configurador Coruña", docs_url=None, redoc_url=None)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["GET", "OPTIONS"],
        allow_headers=["*"],
    )

    async def current_network():
        nonlocal loaded_at
        now = time.time()
        if catalog.network is None or now - loaded_at > STATIC_TTL_SECONDS:
            await catalog.refresh()
            loaded_at = time.time()
        return catalog.network

    @app.get("/", response_class=HTMLResponse)
    async def home() -> str:
        return HOME_HTML

    @app.get("/feeds")
    async def feeds():
        await current_network()
        return [catalog.feed_document()]

    @app.get("/feeds/service-areas")
    async def service_areas():
        network = await current_network()
        return network.service_area()

    @app.get("/stops/within/{bbox}")
    async def stops_within(bbox: str):
        network = await current_network()
        try:
            west, south, east, north = (float(part) for part in bbox.split(","))
        except ValueError as exc:
            raise HTTPException(status_code=400, detail="bbox debe ser lon1,lat1,lon2,lat2") from exc
        west, east = sorted((west, east))
        south, north = sorted((south, north))
        if bbox_area_km2(west, south, east, north) > MAX_AREA_KM2:
            return JSONResponse(
                status_code=400,
                content={
                    "message": "Search area too large (max 5km^2)",
                    "error": "Bad Request",
                    "statusCode": 400,
                },
            )
        stops = network.stops_within(west, south, east, north)
        return [
            {
                "stopId": stop.global_id,
                "stopCode": str(stop.id),
                "name": stop.name,
                "lat": stop.lat,
                "lon": stop.lon,
            }
            for stop in sorted(stops, key=lambda stop: stop.id)
        ]

    @app.get("/stops/{stop_id}/routes")
    async def routes_for_stop(stop_id: str):
        network = await current_network()
        try:
            stop_local = local_id(stop_id)
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        stop = network.stops.get(stop_local)
        if stop is None:
            raise HTTPException(status_code=404, detail="Parada no encontrada")
        routes = []
        for line_id in stop.lines:
            line = network.lines.get(line_id)
            if line is None:
                continue
            routes.append(
                {
                    "routeId": global_id(line.id),
                    "name": line.name,
                    "color": line.color,
                    "headsigns": headsigns_at_stop(line, stop.id, network.stops),
                }
            )
        return routes

    @app.get("/schedule/{pairs:path}")
    async def schedule(pairs: str, limit: str | None = Query(default=None), listMode: str | None = Query(default=None)):
        network = await current_network()
        try:
            parsed = parse_route_stop_pairs(pairs)
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        if len(parsed) > MAX_PAIRS:
            raise HTTPException(status_code=400, detail="Too many route-stop pairs; maximum 25")
        capped = _limit(limit, MAX_HTTP_LIMIT, default=10)
        trips = await _trips_for(catalog, network, parsed, capped, listMode)
        return {"trips": trips}

    return app


app = create_public_app()
