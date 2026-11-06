# ESPHome Component

The drawing code started from the [open source](https://github.com/tjhorner/esphome-transit-tracker) component. This fork vendors it in `firmware/components/transit_tracker` and replaces the WebSocket client with direct requests to iTranvías. Since ESPHome supports such a [wide array of display types](https://esphome.io/components/#display-hardware-platforms), you could easily create your own Transit Tracker which uses an e-paper display, for example.

Additionally, you can fork the component to make your own customizations then re-compile the [stock Transit Tracker firmware](https://github.com/EastsideUrbanism/transit-tracker/blob/main/firmware/transit-tracker.yaml) with your changes. Simply replace the component's GitHub repository with your own in the config's `external_components:` section.

You can learn more about the component in its [GitHub repository](https://github.com/tjhorner/esphome-transit-tracker).