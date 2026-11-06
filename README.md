# Transit Tracker Coru

<p align="center">
  <img src="docs/static/img/transit-tracker.webp" alt="Transit Tracker Logo" width="400" />
</p>

Este repositorio es un fork de Transit Tracker que lo adapta para funcionar con (y solo con) la API de iTranvías (A Coruña).

El panel no cambia de protocolo. El directorio [`api/`](api/README.md) es un adaptador: lee `https://itranvias.com/queryitr_v3.php` y publica el REST y el WebSocket que ya consumen el configurador y el firmware. No hay GTFS ni otras ciudades.

```bash
cd api
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python -m itranvias_adapter
```

En el configurador, pestaña Advanced, apunta el API a `http://<servidor>:8080`. El feed resultante es `coruna`.

La documentación del proyecto original está [aquí](https://eastsideurbanism.github.io/transit-tracker). La de iTranvías, [aquí](https://pedrorey.es/itranvias_api/itranvias_api.html).

## Proyectos relacionados

- [iTranvías API](https://github.com/peprolinbot/itranvias_api) &mdash; wrapper y documentación de la API queryitr de A Coruña
- [Transit Tracker ESPHome Component](https://github.com/tjhorner/esphome-transit-tracker) &mdash; componente ESPHome del firmware, sin cambios de protocolo
- [Transit Tracker Configurator](https://github.com/tjhorner/transit-tracker-configurator) &mdash; el configurador web que sigue usando el panel
