# IR2MQTT (dev)

Web UI for IR bridges with learning, macros, IR databases, Kookong offline
matching and MQTT auto-discovery for Home Assistant.

This add-on is built **from source** (no prebuilt image) so it can ship the
Kookong offline IR database together with its native host library.

## About

IR2MQTT connects physical infrared devices to Home Assistant through one or more
IR bridges (e.g. an ESP32/ESP8266 running the
[ir2mqtt_bridge](https://github.com/steelcuts/ir2mqtt_bridge) ESPHome firmware).
Bridges talk to this app over MQTT — no USB or serial connection to the host is
needed.

This build is a custom variant of [IR2MQTT](https://github.com/steelcuts/ir2mqtt)
that adds:

- **Kookong offline IRDB provider** — the consumer-IR database from the
  OnePlus/OPPO app (`kkoffline.db`) with a native host library built in CI, so
  brand/remote matching works without a cloud account.
- **Region-aware match ranking** — candidates are ranked exactly like the phone
  app: the rank is per country (`RcBrandRemoteMap.rank`), and when the selected
  region has no data the app falls back to the database default (`CN`).
- **Backup & restore** — server-side snapshots of devices/automations kept in
  `/data/backups` (newest 20), plus `merge` / `replace` semantics on import and
  an automatic snapshot before a destructive replace.

## Features

| Feature | Description |
| --- | --- |
| Learning | Capture IR codes from a physical remote through the bridge receiver |
| IR databases | Import Flipper-IRDB / Probono IRDB, browse, search and send codes |
| Kookong match | Pick device type → brand → remote like the phone app, with the region selector |
| Devices & buttons | Name codes, build remote layouts, send them to a bridge |
| Macros / automations | Trigger IR commands from MQTT messages |
| Home Assistant | Auto-discovery over MQTT (`climate`, `button`, …) |
| Backups | Create/list/download/restore/delete snapshots inside the app |

## Installation

1. Add this repository to Home Assistant (link below, or **Settings → Add-ons →
   Add-on Store → ⋮ → Repositories → paste** `https://github.com/khoazero123/ir2mqtt`).
2. Install **IR2MQTT (dev)** — the first install builds the image locally, which
   takes a few minutes on a Raspberry Pi.
3. Open the **Configuration** tab and set your MQTT broker (and credentials) if
   they differ from the defaults.
4. Start the add-on and open the Web UI.

## Configuration

| Option | Description | Default |
| --- | --- | --- |
| `mqtt_broker` | Hostname or IP of your MQTT broker (`core-mosquitto` for the Home Assistant Mosquitto broker) | `10.10.20.104` |
| `mqtt_port` | MQTT broker port | `1883` |
| `mqtt_user` | MQTT username | *(empty)* |
| `mqtt_pass` | MQTT password | *(empty)* |
| `log_level` | `DEBUG`, `INFO`, `WARNING` or `ERROR` | `INFO` |
| `kookong_brands` | Comma-separated brand filter for the Kookong import (empty = all brands) | *(empty)* |
| `kookong_max_remotes` | Maximum remotes imported from the Kookong database | `1200` |
| `kookong_max_remotes_per_brand` | Maximum remotes imported per brand | `20` |
| `kookong_max_keys_per_remote` | Maximum keys imported per remote | `64` |
| `kookong_include_ac` | Import air-conditioner remotes from Kookong | `false` |
| `kookong_ac_remotes` | How many AC remotes to import when enabled | `0` |

> Importing the full Kookong database is the app's heaviest operation
> (10k+ remotes / 380k+ buttons). It runs in bounded chunks; on a 4 GB host give
> the import some headroom and keep other memory-hungry add-ons stopped.

## Requirements

- Home Assistant OS or Supervised installation (this is a Supervisor add-on).
- An MQTT broker, e.g. the official **Mosquitto broker** add-on.
- At least one IR bridge running the IR2MQTT ESPHome firmware.

## Support

Found a bug or have a question? Open an issue in
[khoazero123/ir2mqtt](https://github.com/khoazero123/ir2mqtt/issues).

## Credits & License

Add-on packaging, Kookong provider, region ranking and backup/restore are custom
work. The upstream project is [steelcuts/ir2mqtt](https://github.com/steelcuts/ir2mqtt)
(MIT). Kookong SDK sources under `kksdk-src/` are a portable host build used for
the offline database; `kkoffline.db` originates from the consumer IR app.

MIT License — see [LICENSE.md](../LICENSE.md).
