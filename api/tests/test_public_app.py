import unittest

from fastapi.testclient import TestClient

from app import create_public_app
from tests.test_adapter import FakeClient


class PublicAppTests(unittest.TestCase):
    def setUp(self):
        self.client = TestClient(create_public_app(FakeClient()))

    def test_home_explains_the_configurator_url(self):
        response = self.client.get("/", headers={"Origin": "https://countdown-config.horner.tj"})
        self.assertEqual(response.status_code, 200)
        self.assertIn("Advanced", response.text)
        self.assertEqual(response.headers["access-control-allow-origin"], "*")

    def test_feeds_routes_and_schedule(self):
        feeds = self.client.get("/feeds")
        self.assertEqual(feeds.status_code, 200)
        self.assertEqual(feeds.json()[0]["code"], "coruna")

        routes = self.client.get("/stops/coruna:1/routes")
        names = {route["name"]: route for route in routes.json()}
        self.assertEqual(names["1"]["routeId"], "coruna:100")

        too_big = self.client.get("/stops/within/-9,43,-7,44")
        self.assertEqual(too_big.status_code, 400)

        schedule = self.client.get(
            "/schedule/coruna:100,coruna:1?limit=2",
            headers={"Origin": "https://countdown-config.horner.tj"},
        )
        self.assertEqual(schedule.status_code, 200)
        self.assertLessEqual(len(schedule.json()["trips"]), 2)
        self.assertEqual(schedule.headers["access-control-allow-origin"], "*")
