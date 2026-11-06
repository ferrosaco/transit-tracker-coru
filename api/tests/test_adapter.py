import json
import unittest

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from itranvias_adapter.network import direction_for_bus, parse_network
from itranvias_adapter.schedule import (
    LiveBus,
    build_trips,
    eta_timestamp,
    parse_buses,
    parse_route_stop_pairs,
)
from itranvias_adapter.server import create_app

FIXTURE = json.loads(
    """
{
  "resultado": "OK",
  "iTranvias": {
    "actualizacion": {
      "fecha": "20261106T190000",
      "lineas": [
        {
          "id": 100,
          "lin_comer": "1",
          "nombre_orig": "Abente y Lago",
          "nombre_dest": "Castrillón",
          "color": "982135",
          "rutas": [
            {"ruta": 10000, "paradas": [523, 180, 1, 2, 68]},
            {"ruta": 10001, "paradas": [68, 69, 523]}
          ]
        },
        {
          "id": 200,
          "lin_comer": "2",
          "nombre_orig": "Abente y Lago",
          "nombre_dest": "Los Castros",
          "color": "0055AA",
          "rutas": [
            {"ruta": 20000, "paradas": [1, 8]},
            {"ruta": 20001, "paradas": [8, 1]}
          ]
        }
      ],
      "paradas": [
        {"id": 1, "nombre": "Puerta Real", "posx": -8.39585, "posy": 43.370115, "enlaces": [100, 200]},
        {"id": 2, "nombre": "Plaza de María Pita", "posx": -8.396, "posy": 43.371, "enlaces": [100]},
        {"id": 8, "nombre": "Los Castros", "posx": -8.41, "posy": 43.35, "enlaces": [200]},
        {"id": 68, "nombre": "Castrillón", "posx": -8.42, "posy": 43.36, "enlaces": [100]},
        {"id": 69, "nombre": "Vuelta", "posx": -8.40, "posy": 43.36, "enlaces": []},
        {"id": 180, "nombre": "Antes", "posx": -8.39, "posy": 43.37, "enlaces": []},
        {"id": 523, "nombre": "Abente y Lago", "posx": -8.38, "posy": 43.37, "enlaces": []}
      ]
    }
  }
}
"""
)

ARRIVALS = {
    "resultado": "OK",
    "buses": {
        "parada": 1,
        "lineas": [
            {
                "linea": 100,
                "buses": [
                    {"bus": 378, "tiempo": "6", "distancia": "100", "estado": 1, "ult_parada": 180},
                    {"bus": 421, "tiempo": "4", "distancia": "100", "estado": 1, "ult_parada": 69},
                    {"bus": 1, "tiempo": "<1", "distancia": "10", "estado": 1, "ult_parada": 180},
                ],
            },
            {
                "linea": 200,
                "buses": [
                    {"bus": 391, "tiempo": "14", "distancia": "100", "estado": 0, "ult_parada": 1}
                ],
            },
        ],
    },
}


class FakeClient:
    def __init__(self):
        self.base_url = "https://itranvias.com/queryitr_v3.php"

    async def fetch_static(self):
        return FIXTURE

    async def fetch_stop(self, stop_id, cache_seconds=10):
        if stop_id != 1:
            return {"resultado": "OK", "buses": {"parada": stop_id, "lineas": []}}
        return ARRIVALS

    async def close(self):
        return None


class ScheduleTests(unittest.TestCase):
    def setUp(self):
        self.network = parse_network(FIXTURE)

    def test_direction_uses_last_stop(self):
        line = self.network.lines[100]
        self.assertEqual(direction_for_bus(line, 1, 180), 0)
        # La última parada no está en ningún recorrido de esta parada: queda el único sentido.
        self.assertEqual(direction_for_bus(line, 1, 69), 0)

    def test_return_direction_when_stop_is_on_it(self):
        line = self.network.lines[200]
        self.assertEqual(direction_for_bus(line, 1, 8), 1)

    def test_walk_time_hides_a_bus_you_cannot_catch(self):
        now = 1_700_000_000
        bus = LiveBus(100, "1", 5, False, 180)
        self.assertIsNone(eta_timestamp(now, bus, -300))
        shown = eta_timestamp(now, LiveBus(100, "1", 6, False, 180), -300)
        self.assertIsNotNone(shown)
        self.assertGreater(shown, now)

    def test_trips_keep_both_directions_and_apply_limit(self):
        pairs = parse_route_stop_pairs("coruna:100,coruna:1,-120")
        buses = {1: parse_buses(ARRIVALS)}
        now = 1_700_000_000
        trips = build_trips(self.network, pairs, buses, now, limit=10, list_mode="sequential")
        headsigns = [trip["headsign"] for trip in trips]
        self.assertIn("Castrillón", headsigns)
        self.assertTrue(all(trip["isRealtime"] for trip in trips))
        self.assertTrue(all(trip["routeName"] == "1" for trip in trips))
        # 4 minutos menos 2 de camino sigue; el de <1 minuto con 2 de camino no.
        self.assertEqual(len(trips), 2)

    def test_next_per_route_keeps_one_per_direction(self):
        pairs = parse_route_stop_pairs("coruna:100,coruna:1;coruna:200,coruna:1")
        buses = {1: parse_buses(ARRIVALS)}
        trips = build_trips(
            self.network,
            pairs,
            buses,
            1_700_000_000,
            limit=10,
            list_mode="nextPerRoute",
        )
        keys = {(trip["routeId"], trip["directionId"]) for trip in trips}
        self.assertEqual(len(keys), len(trips))


class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.server = TestServer(create_app(FakeClient()))
        self.client = TestClient(self.server)
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()

    async def test_home_and_stop_search(self):
        home = await self.client.get("/")
        text = await home.text()
        self.assertEqual(home.status, 200)
        self.assertIn("Buses de A Coruña", text)
        found = await self.client.get("/stops", params={"q": "puerta"})
        stops = await found.json()
        self.assertEqual(stops[0]["name"], "Puerta Real")
        self.assertEqual(stops[0]["stopId"], "coruna:1")

    async def test_feeds_and_stop_routes(self):
        feeds = await self.client.get("/feeds")
        body = await feeds.json()
        self.assertEqual(body[0]["code"], "coruna")
        self.assertEqual(body[0]["name"], "Tranvías de A Coruña")
        self.assertEqual(len(body), 1)

        routes = await self.client.get("/stops/coruna:1/routes")
        listed = await routes.json()
        names = {route["name"]: route for route in listed}
        self.assertEqual(names["1"]["routeId"], "coruna:100")
        self.assertIn("Castrillón", names["1"]["headsigns"])
        self.assertEqual(names["1"]["color"], "982135")

    async def test_bbox_limit_and_stops(self):
        too_big = await self.client.get("/stops/within/-9,43,-7,44")
        self.assertEqual(too_big.status, 400)
        near = await self.client.get("/stops/within/-8.3965,43.3695,-8.3950,43.3708")
        stops = await near.json()
        self.assertEqual([stop["stopId"] for stop in stops], ["coruna:1"])

    async def test_schedule_and_websocket(self):
        response = await self.client.get("/schedule/coruna:100,coruna:1?limit=2")
        payload = await response.json()
        self.assertEqual(response.status, 200)
        self.assertLessEqual(len(payload["trips"]), 2)
        self.assertTrue(payload["trips"][0]["arrivalTime"] > 1_000_000_000)

        socket = await self.client.ws_connect("/")
        await socket.send_json(
            {
                "event": "schedule:subscribe",
                "data": {
                    "routeStopPairs": "coruna:200,coruna:1",
                    "limit": 3,
                    "listMode": "sequential",
                    "sortByDeparture": True,
                },
            }
        )
        events = []
        while len(events) < 2:
            message = await socket.receive_json(timeout=5)
            events.append(message["event"])
        self.assertIn("heartbeat", events)
        self.assertIn("schedule", events)
        await socket.close()


if __name__ == "__main__":
    unittest.main()
