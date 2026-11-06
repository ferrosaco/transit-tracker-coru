from __future__ import annotations

import asyncio
import time
from typing import Any

import aiohttp

STATIC_QUERY = "20160101T000000_es_0_20160101T000000"
DEFAULT_URL = "https://itranvias.com/queryitr_v3.php"
USER_AGENT = "TransitTrackerCoruna/1.0 (+https://pedrorey.es/itranvias_api/itranvias_api.html)"


class ItranviasClient:
    def __init__(self, base_url: str = DEFAULT_URL, session: aiohttp.ClientSession | None = None):
        self.base_url = base_url
        self._session = session
        self._owns_session = session is None
        self._arrivals_cache: dict[int, tuple[float, dict[str, Any]]] = {}
        self._arrivals_lock = asyncio.Lock()

    async def close(self) -> None:
        if self._owns_session and self._session is not None:
            await self._session.close()

    async def _http(self) -> aiohttp.ClientSession:
        if self._session is None or self._session.closed:
            timeout = aiohttp.ClientTimeout(total=25)
            self._session = aiohttp.ClientSession(
                timeout=timeout,
                headers={"User-Agent": USER_AGENT, "Accept": "application/json"},
            )
            self._owns_session = True
        return self._session

    async def _get(self, params: dict[str, str]) -> dict[str, Any]:
        session = await self._http()
        async with session.get(self.base_url, params=params) as response:
            response.raise_for_status()
            payload = await response.json(content_type=None)
        if not isinstance(payload, dict):
            raise ValueError("iTranvías no devolvió un objeto JSON")
        return payload

    async def fetch_static(self) -> dict[str, Any]:
        return await self._get({"func": "7", "dato": STATIC_QUERY})

    async def fetch_stop(self, stop_id: int, *, cache_seconds: float = 10) -> dict[str, Any]:
        now = time.monotonic()
        cached = self._arrivals_cache.get(stop_id)
        if cached is not None and now - cached[0] < cache_seconds:
            return cached[1]
        async with self._arrivals_lock:
            cached = self._arrivals_cache.get(stop_id)
            now = time.monotonic()
            if cached is not None and now - cached[0] < cache_seconds:
                return cached[1]
            payload = await self._get({"func": "0", "dato": str(stop_id)})
            self._arrivals_cache[stop_id] = (time.monotonic(), payload)
            return payload
