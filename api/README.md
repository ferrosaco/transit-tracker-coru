# API iTranvías para Transit Tracker

Este servicio habla el mismo protocolo que espera el Transit Tracker (REST para el configurador y WebSocket para el panel) y obtiene los datos solo de la API `queryitr` de iTranvías, la de la Compañía de Tranvías de La Coruña.

No usa GTFS, OneBusAway ni ninguna otra red.

Documentación de origen: <https://pedrorey.es/itranvias_api/itranvias_api.html>

## Arranque

```bash
cd api
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python -m itranvias_adapter
```

O con Docker:

```bash
docker compose up --build
```

Queda en el puerto 8080.

En el configurador, pestaña Advanced, la URL del API es `http://<tu-servidor>:8080`. El configurador la convierte a `ws://` al enviarla al dispositivo. El feed es uno solo, `coruna`, y los identificadores salen como `coruna:100` (línea) y `coruna:1` (parada).

## Qué devuelve

iTranvías solo publica buses que están en servicio. Cada llegada es en tiempo real (`isRealtime: true`) y el tiempo es el que da la compañía, en minutos. Si una línea no tiene buses activos, no aparece: el panel muestra que no hay próximas salidas. No hay horario planificado de respaldo.

El cartel de destino se deduce del recorrido: la primera ruta de la línea va hacia `nombre_dest`, la segunda hacia `nombre_orig`, y el resto hacia su última parada. La última parada que reporta el bus elige el sentido.
