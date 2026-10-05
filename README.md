# IR2MQTT HA App Repository (khoazero123 fork)

Home Assistant add-on repository for **IR2MQTT (dev)** — a build-from-source
variant of [IR2MQTT](https://github.com/steelcuts/ir2mqtt) with:

- Kookong offline IR database provider (native `libkksdk_host.so`)
- Brand/remote **match wizard** with **region-aware ranking** (rank per
  country, mirroring the Kookong mobile app)
- **Backup & restore** for devices/automations (`/data/backups`, keep 20) with
  `merge` / `replace` import modes

Forked from [steelcuts/ir2mqtt-ha-app](https://github.com/steelcuts/ir2mqtt-ha-app).

## Install

1. Home Assistant → **Settings → Add-ons → Add-on Store**
2. **⋮ → Repositories** → add `https://github.com/khoazero123/ir2mqtt-ha-app`
3. Install **IR2MQTT (dev)** (slug `ir2mqtt_dev`) — built locally from source
   on first install/rebuild.

## Add-ons

| Add-on | Folder | Notes |
|---|---|---|
| IR2MQTT (dev) | `ir2mqtt_dev/` | Builds from source in this repo (no prebuilt image) |
| IR2MQTT | `ir2mqtt/` | Upstream stock add-on (prebuilt GHCR image) |

## License

MIT — see [steelcuts/ir2mqtt](https://github.com/steelcuts/ir2mqtt).
