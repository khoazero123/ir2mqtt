# Home Assistant Add-on Repository: IR2MQTT (dev)

[![Release][release-shield]][release]
[![License][license-shield]](LICENSE.md)
![Project Stage][project-stage-shield]

![Supports aarch64 Architecture][aarch64-shield]
![Supports amd64 Architecture][amd64-shield]

Web UI for IR bridges with learning, macros, IR databases, **Kookong offline
matching** and MQTT auto-discovery for Home Assistant.

## About

[IR2MQTT][upstream] bridges physical infrared devices with Home Assistant:
instead of writing YAML you learn codes from your remotes, browse public IR
databases and send commands through one or more IR bridges that connect to the
app over MQTT.

This repository is the **custom add-on repository** for a build-from-source
variant that ships:

- the **Kookong offline IR database** (`kkoffline.db`, consumer IR from the
  OnePlus/OPPO app) together with its native host library, compiled during the
  add-on build;
- **region-aware match ranking**, so the brand/remote picker ranks candidates
  exactly like the phone app (rank per country, falling back to the database
  default region when a country has no data);
- **backup & restore** for devices/automations (server-side snapshots, newest
  20, `merge`/`replace` import with an automatic safety snapshot).

The add-on is built locally on your Home Assistant machine from the
`ir2mqtt_dev/` folder of this repository — there is no prebuilt image.

[:books: Read the full add-on documentation][documentation]

## Installation

Add this repository to Home Assistant using
[this link][repository], by clicking the button below, or manually via
**Settings → Add-ons → Add-on Store → ⋮ → Repositories → paste**
`https://github.com/khoazero123/ir2mqtt`.

[![Add Repository to HA][my-ha-badge]][my-ha-url]

[![Open your Home Assistant instance and show the add-on page][my-ha-addon-badge]][my-ha-addon-url]

Then install **IR2MQTT (dev)** from the store, set your MQTT broker in the
Configuration tab and start it. The first installation builds the image locally
(a few minutes on a Raspberry Pi).

Requirements: Home Assistant OS / Supervised, an MQTT broker (e.g. the Mosquitto
broker add-on) and at least one IR bridge running the
[ir2mqtt_bridge][bridge] ESPHome firmware.

## Configuration

See the [add-on documentation][documentation] for the full option reference
(MQTT broker, log level and the Kookong import limits).

## Support

Got questions or found a bug? [Open an issue here][issue] on GitHub.

## Credits & Authors

Based on [steelcuts/ir2mqtt][upstream] by
[steelcuts][steelcuts] — the add-on packaging, Kookong provider, region ranking
and backup/restore in this repository are custom work by
[khoazero123][maintainer].

## License

MIT License — see [LICENSE.md](LICENSE.md).

[aarch64-shield]: https://img.shields.io/badge/aarch64-yes-green.svg
[amd64-shield]: https://img.shields.io/badge/amd64-yes-green.svg
[bridge]: https://github.com/steelcuts/ir2mqtt_bridge
[documentation]: ir2mqtt_dev/DOCS.md
[issue]: https://github.com/khoazero123/ir2mqtt/issues
[license-shield]: https://img.shields.io/github/license/khoazero123/ir2mqtt
[maintainer]: https://github.com/khoazero123
[my-ha-addon-badge]: https://my.home-assistant.io/badges/supervisor_addon.svg
[my-ha-addon-url]: https://my.home-assistant.io/redirect/supervisor_addon/?addon=372e41ba_ir2mqtt_dev&repository_url=https%3A%2F%2Fgithub.com%2Fkhoazero123%2Fir2mqtt
[my-ha-badge]: https://my.home-assistant.io/badges/supervisor_add_addon_repository.svg
[my-ha-url]: https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2Fkhoazero123%2Fir2mqtt
[project-stage-shield]: https://img.shields.io/badge/project%20stage-custom%20build-orange.svg
[release-shield]: https://img.shields.io/github/v/tag/khoazero123/ir2mqtt?include_prereleases&label=version
[release]: https://github.com/khoazero123/ir2mqtt/tags
[repository]: https://github.com/khoazero123/ir2mqtt
[steelcuts]: https://github.com/steelcuts
[upstream]: https://github.com/steelcuts/ir2mqtt
