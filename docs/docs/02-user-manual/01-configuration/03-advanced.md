# Advanced

This section includes more advanced or uncommon options.

## API Server

La URL pública es `https://configurador-coru.vercel.app`. Solo hace falta para elegir la parada en el mapa. El panel no la usa para las llegadas: las pide directamente a iTranvías. El servicio está descrito en [API Server](../../03-advanced/api-server.md).

## Display Orientation

:::info
Starting in firmware version 2.7.0, the display orientation is automatically detected using the built-in accelerometer, so this option has no effect.
:::

If you want to mount your Transit Tracker so that the USB-C port and buttons are on the left side instead of the right (i.e., if you have a short cable), you can flip the display orientation with this option. Flipping the orientation will also swap the order of the brightness control buttons to match.

## Localization

The Transit Tracker UI will show time units ("Now", "minutes", etc.) in English by default. You can customize these labels if you speak a different language.