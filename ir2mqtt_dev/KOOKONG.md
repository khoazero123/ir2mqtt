# Kookong IRDB provider (OnePlus / OPPO Consumer IR)

Adds the **Kookong offline IR database** (`kkoffline.db`, ~11k remotes / ~330k keys)
as a fourth IR2MQTT IR-database provider, alongside Flipper and Probono.

## Why not use the OEM `libkksdk.so`?

The Kookong SDK ships `libkksdk.so` for Android (`arm64-v8a`). It **cannot** be
loaded on Home Assistant / Raspberry Pi 5:

| Blocker | Detail |
|---|---|
| bionic dependencies | `NEEDED liblog.so, libc.so, libm.so, libdl.so` — Android-only sonames (no version suffix); `liblog.so` does not exist on Linux |
| symbol versioning | requires version node `LIBC` (bionic); glibc uses `GLIBC_2.x`, musl has none → `version 'LIBC' not found` |
| JNI-only exports | all 20 exports are `Java_com_hzy_tvmao_*`; requires `JNIEnv*` / `JavaVM`, no plain C API |

Verified on HAOS 17 / Raspberry Pi 5: fails under Alpine/musl (HA add-on default),
fails under Debian/glibc, and still fails with all four dependencies stubbed out.

**Instead** this add-on builds `libkksdk_host.so` from the vendored
reverse-engineered sources (`kksdk-src/`, from `1-plus/kksdklib-codex-new`). That
build exposes a portable C ABI (`kksdk_irdevice_encode_pulse`,
`kksdk_remote_encoder_*`, `streamhelper_transform_*`) with no JNI and no bionic,
and it builds and runs natively on the HA Alpine base image.

The upstream reverse engineering is parity-verified: 887,749/887,749 non-AC
pulses and 708/708 AC A/B frames match the OEM library.

## How it works

1. `kkoffline.db` is bundled at `/usr/local/share/kookong/kkoffline.db`.
2. `libkksdk_host.so` is built in a Docker stage and installed to `/usr/lib/`.
3. `backend/providers/kookong.py` reads the DB through
   `backend/kookong/kksdk_db_common.py` (`KkHost` ctypes binding):
   - `RcRemoteController.remote_id` / `RcCountryBrand.name` are encrypted tokens → `KkHost.decrypt_token`
   - `RcRemoteKey.pulse_data` is an encrypted BLOB → `KkHost.decrypt_bytes`
   - keys carrying a duration CSV are used directly; the rest go through
     `KkHost.encode_pulse` (non-AC) or `KkHost.encode_ac` (air conditioners)
4. Remotes are grouped **device type first, then brand**, so the picker reads
   `kookong/<DeviceType>/<Brand>/<Brand>_<remoteId>` — e.g.
   `kookong/Air_Conditioner/DaiKin/DaiKin_10502` with display name
   `Air Conditioner DaiKin 10502`. Device types come from
   `RcBrandRemoteMap.device_type_id` and are walked in ascending id order
   (1 TV, 2 Set-top Box, 3 Projector, 4 Audio, 5 Air Conditioner, ...).
5. Buttons are emitted as IR2MQTT `raw` codes:

   ```json
   {"protocol": "raw", "payload": {"timings": [9105, -4553, 553, -553], "frequency": 38000}}
   ```

   Sign convention: positive = mark, negative = space, starting with a mark.
   `frequency` is omitted when it is the 38 kHz default.

## Configuration

Set through add-on options (HA → Add-on → Configuration) or environment variables.

| Option | Env | Default | Meaning |
|---|---|---|---|
| `kookong_brands` | `KKOOKONG_BRANDS` | *(empty = all)* | Comma list of brand names to import, e.g. `Samsung,LG,DaiKin`. Case-insensitive. `*` = all |
| `kookong_max_remotes` | `KKOOKONG_MAX_REMOTES` | `1200` | Global cap on imported remotes |
| `kookong_max_remotes_per_brand` | `KKOOKONG_MAX_REMOTES_PER_BRAND` | `20` | Per-brand cap (remotes ordered by popularity rank) |
| `kookong_max_keys_per_remote` | `KKOOKONG_MAX_KEYS_PER_REMOTE` | `64` | Buttons per remote |
| `kookong_include_ac` | `KKOOKONG_INCLUDE_AC` | `false` | Import air-conditioner remotes |
| `kookong_ac_remotes` | `KKOOKONG_AC_REMOTES` | `0` | How many AC remotes to expand (each yields a state grid) |
| — | `KKOOKONG_AC_MODES` | `0,1,2,3,4` | AC modes: 0=auto 1=cool 2=heat 3=fan 4=dry |
| — | `KKOOKONG_AC_TEMPS` | `16..30` | AC temperatures |
| — | `KKOOKONG_AC_INCLUDE_OFF` | `true` | Add a `Power Off` button per AC remote |
| — | `KKOOKONG_MIN_KEYS` | `4` | Skip remotes with fewer decoded buttons |
| — | `KKOOKONG_DB` | auto | Override the `kkoffline.db` path |
| — | `KKOOKONG_LIB` | auto | Override the `libkksdk_host.so` path |

### Notes on scope

- The full DB has ~330k keys. Importing everything as raw timings would bloat the
  IR2MQTT database, so the provider is capped by default (1200 remotes / 20 per
  brand). Raise the caps or set `kookong_brands` for a focused import. A cap of
  `0` means **unlimited**.
- **Caps interact with the type-first order.** `kookong_max_remotes` is a global
  budget consumed in walk order (device type ascending, then brand, then rank),
  so a small global cap will exhaust on the first device types (TV, Set-top Box)
  and later types (AC, Fan, …) never appear. Use `kookong_brands` to narrow the
  import, or `0` (unlimited) for a complete one.
- `kookong_max_remotes_per_brand` counts per **(device type, brand)** pair, which
  is the right granularity now that the tree is type-first.
- **Duplicate remotes are collapsed.** A remote is commonly mapped to several
  brands *and* several device types in `RcBrandRemoteMap` (32,378 mappings for
  11,030 unique remotes). Without de-duplication the same codes are imported
  ~2.6x under different folders. Each `remote_id` is imported once, under the
  first position it is found in (device type ascending, then brand, then
  popularity rank). Skipped duplicates are reported as `duplicate_remote` in the
  import stats.
- A full unlimited import yields roughly **11,030 remotes / ~386,000 buttons**
  (10,176 non-AC + 854 AC remotes) and takes on the order of 10-25 minutes on a
  Raspberry Pi 5.
- **Air conditioners** are stateful: one key does not produce one fixed code.
  IR2MQTT has no climate entity, so AC remotes are expanded into discrete
  `Power Off` / `On <Mode> <Temp>C` raw buttons. AC import is off by default
  because the grid multiplies quickly.
- If `kkoffline.db` is missing the provider logs a warning and returns no remotes;
  the other providers still run.
- **Memory**: the provider yields remotes lazily and `IrDbManager` writes them in
  bounded chunks, so a full-database import stays flat in RAM (tens of MB rather
  than growing into the gigabytes).

## Rebuilding the native library

The Dockerfile builds it automatically:

```dockerfile
FROM ${BUILD_FROM} AS kksdk-builder
RUN apk add --no-cache build-base cmake
COPY kksdk-src/CMakeLists.txt /kksdk-src/
COPY kksdk-src/src /kksdk-src/src
RUN cmake -S /kksdk-src -B /kksdk-build -DCMAKE_BUILD_TYPE=Release -DKKSDK_BUILD_HOST_TESTS=OFF \
 && cmake --build /kksdk-build -j"$(nproc)"
```

`libstdc++` is explicitly installed in the runtime stage so it survives
`apk del g++`.
