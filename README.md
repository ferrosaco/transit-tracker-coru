# Transit Tracker Coru

<p align="center">
  <img src="docs/static/img/transit-tracker.webp" alt="Transit Tracker Logo" width="400" />
</p>

Este repositorio es un fork de Transit Tracker solo para los buses de A Coruña. El panel llama él mismo a la API `queryitr` de iTranvías (`https://itranvias.com/queryitr_v3.php`). No hay GTFS ni otras ciudades, y no hace falta dejar un ordenador encendido para que se vean las llegadas.

El configurador de siempre sigue sirviendo para elegir la parada. El mapa de ese configurador usa el adaptador de [`api/`](api/README.md), y ese proceso se puede apagar en cuanto la parada queda grabada en el panel.

## Panel que ya está montado

La parada que hubiera guardada de un GTFS antiguo no se reutiliza. Hay que grabar este firmware y volver a elegirla.

1. Pulsa los dos botones del panel a la vez hasta que aparezca la IP. Ábrela en el navegador y apaga **Auto-Update**. El programa original, si sigue con la actualización automática, puede reinstalarse y deshacer el cambio. El botón «Update firmware» del configurador instala ese programa original, no este.
2. Descarga [`docs/static/firmware/firmware.ota.bin`](docs/static/firmware/firmware.ota.bin) (el archivo se llama `firmware.ota.bin`, no el factory). En la misma web del panel, sección **OTA Update**, elige ese archivo y pulsa Update. Si esa sección no aparece, hay que grabarlo por USB con ESPHome 2026.9.1: `esphome run transit-tracker.yaml` dentro de `firmware/`.
3. En un ordenador de la misma red, arranca el adaptador solo mientras configuras:

   ```bash
   cd api
   python3 -m venv .venv
   . .venv/bin/activate
   pip install -r requirements.txt
   python -m itranvias_adapter
   ```

4. Abre el configurador, pestaña Advanced, y pon de URL del API `http://<ip-de-ese-ordenador>:8080`. Elige la parada y las líneas y guarda, igual que siempre. Los identificadores quedan como `coruna:100` (línea 1) y `coruna:1` (una parada).
5. Para el adaptador. El panel sigue preguntando a iTranvías por su cuenta cada 15 segundos. Si una línea no tiene buses en servicio, no hay horario de reserva: el panel enseña que no hay próximas llegadas.

La documentación del proyecto original está [aquí](https://eastsideurbanism.github.io/transit-tracker). La de iTranvías, [aquí](https://pedrorey.es/itranvias_api/itranvias_api.html).

## Proyectos relacionados

- [iTranvías API](https://github.com/peprolinbot/itranvias_api) &mdash; envoltura y documentación de la API queryitr de A Coruña
- [Transit Tracker ESPHome Component](https://github.com/tjhorner/esphome-transit-tracker) &mdash; componente original; este fork lo incluye en `firmware/components/transit_tracker` y cambia la descarga de horarios
- [Transit Tracker Configurator](https://github.com/tjhorner/transit-tracker-configurator) &mdash; el configurador web que sigue usando el panel
