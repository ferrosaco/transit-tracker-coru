# Advanced

This section includes more advanced or uncommon options.

## API Server

Aquí va la URL del adaptador de iTranvías descrito en [API Server](../../03-advanced/api-server.md). Incluye el esquema, `http://` o `https://`. El configurador la convierte a WebSocket al grabarla en el panel. Sin esta URL el dispositivo no tiene de dónde leer las llegadas de A Coruña.

## Display Orientation

:::info
Starting in firmware version 2.7.0, the display orientation is automatically detected using the built-in accelerometer, so this option has no effect.
:::

If you want to mount your Transit Tracker so that the USB-C port and buttons are on the left side instead of the right (i.e., if you have a short cable), you can flip the display orientation with this option. Flipping the orientation will also swap the order of the brightness control buttons to match.

## Localization

The Transit Tracker UI will show time units ("Now", "minutes", etc.) in English by default. You can customize these labels if you speak a different language.