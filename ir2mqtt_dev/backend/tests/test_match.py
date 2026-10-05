"""Tests for the Kookong match wizard API.

The wizard reads the bundled ``kkoffline.db`` and the native
``libkksdk_host.so``; tests skip automatically when either is missing.
"""

from __future__ import annotations

import os
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest
from fastapi.testclient import TestClient

from backend.main import app

DB_CANDIDATES = [
    Path(os.environ.get("KKOOKONG_DB", "/data/kkoffline.db")),
    Path("/usr/local/share/kookong/kkoffline.db"),
]
LIB_CANDIDATES = [
    Path(os.environ.get("KKOOKONG_LIB", "/usr/lib/libkksdk_host.so")),
    Path("/usr/local/lib/libkksdk_host.so"),
]


def _artifacts_available() -> bool:
    return any(p.exists() for p in DB_CANDIDATES) and any(p.exists() for p in LIB_CANDIDATES)


requires_artifacts = pytest.mark.skipif(
    not _artifacts_available(),
    reason="kkoffline.db and/or libkksdk_host.so not available",
)


def test_match_available_endpoint(client: TestClient):
    response = client.get("/api/match/available")
    assert response.status_code == 200
    assert "available" in response.json()


@requires_artifacts
def test_match_categories_include_air_conditioner(client: TestClient):
    response = client.get("/api/match/categories")
    assert response.status_code == 200
    names = [c["name"] for c in response.json()]
    assert "Air Conditioner" in names
    assert "TV" in names


@requires_artifacts
def test_match_brands_and_candidates_for_ac(client: TestClient):
    categories = client.get("/api/match/categories").json()
    ac = next(c for c in categories if c["name"] == "Air Conditioner")

    brands = client.get(f"/api/match/brands?category_id={ac['id']}").json()
    assert brands, "expected AC brands"

    daikin = next((b for b in brands if b["name"] == "DaiKin"), brands[0])
    candidates = client.get(
        f"/api/match/candidates?category_id={ac['id']}&brand_id={daikin['id']}"
    ).json()
    assert candidates, "expected AC candidates"
    # ranked best-first
    assert candidates[0]["rank"] <= candidates[-1]["rank"]
    assert candidates[0]["is_ac"] is True


@requires_artifacts
def test_match_test_sends_ac_frame(client: TestClient):
    categories = client.get("/api/match/categories").json()
    ac = next(c for c in categories if c["name"] == "Air Conditioner")
    brand = client.get(f"/api/match/brands?category_id={ac['id']}").json()[0]
    candidate = client.get(
        f"/api/match/candidates?category_id={ac['id']}&brand_id={brand['id']}"
    ).json()[0]

    with patch("backend.mqtt.MQTTManager.send_ir_code", new=AsyncMock()) as send:
        response = client.post(
            "/api/match/test",
            json={
                "category_id": ac["id"],
                "brand_id": brand["id"],
                "remote_id": candidate["remote_id"],
                "target": ["s11:ir_tx"],
                "mode": 1,
                "temperature": 25,
            },
        )

    assert response.status_code == 200
    body = response.json()
    assert body["sent"] is True
    assert body["is_ac"] is True
    assert send.await_count == 1
    code = send.await_args.args[0]
    assert code["protocol"] == "raw"
    assert code["payload"]["timings"]


@requires_artifacts
def test_match_save_creates_device(client: TestClient):
    categories = client.get("/api/match/categories").json()
    tv = next(c for c in categories if c["name"] == "TV")
    brand = client.get(f"/api/match/brands?category_id={tv['id']}").json()[0]
    candidate = client.get(
        f"/api/match/candidates?category_id={tv['id']}&brand_id={brand['id']}"
    ).json()[0]

    response = client.post(
        "/api/match/save",
        json={
            "category_id": tv["id"],
            "brand_id": brand["id"],
            "remote_id": candidate["remote_id"],
            "name": "Matched TV",
        },
    )
    assert response.status_code == 200
    device = response.json()
    assert device["name"] == "Matched TV"
    assert len(device["buttons"]) > 0
    assert device["buttons"][0]["code"]["protocol"] == "raw"


@requires_artifacts
def test_match_save_ac_creates_stateful_remote(client: TestClient):
    """An AC remote becomes 6 action keys, not a 76-button state grid."""
    categories = client.get("/api/match/categories").json()
    ac = next(c for c in categories if c["name"] == "Air Conditioner")
    brand = client.get(f"/api/match/brands?category_id={ac['id']}").json()[0]
    candidate = client.get(
        f"/api/match/candidates?category_id={ac['id']}&brand_id={brand['id']}"
    ).json()[0]

    response = client.post(
        "/api/match/save",
        json={
            "category_id": ac["id"],
            "brand_id": brand["id"],
            "remote_id": candidate["remote_id"],
            "name": "Matched AC",
        },
    )
    assert response.status_code == 200
    device = response.json()
    assert len(device["buttons"]) == 6
    actions = {b["code"]["payload"]["action"] for b in device["buttons"]}
    assert actions == {"power", "temp_up", "temp_down", "mode", "fan", "swing"}
    for b in device["buttons"]:
        assert b["code"]["protocol"] == "kookong_ac"

    # AC state is tracked per device
    state = client.get(f"/api/match/ac_state?device_id={device['id']}").json()
    assert state["temperature"] == 25

    # Pressing "Temp +" steps the temperature and transmits the new state
    temp_up = next(b for b in device["buttons"] if b["code"]["payload"]["action"] == "temp_up")
    with patch("backend.mqtt.MQTTManager.send_ir_code", new=AsyncMock()) as send:
        trigger = client.post(
            f"/api/devices/{device['id']}/buttons/{temp_up['id']}/trigger",
            params={"targets": ["s11:ir_tx"]},
        )
    assert trigger.status_code == 200
    assert send.await_count == 1
    assert send.await_args.args[0]["protocol"] == "raw"

    state = client.get(f"/api/match/ac_state?device_id={device['id']}").json()
    assert state["temperature"] == 26

    # Clean up so the test does not leak a device into other tests
    client.delete(f"/api/devices/{device['id']}")


@requires_artifacts
def test_match_test_supports_individual_ac_keys(client: TestClient):
    """The match screen can press individual AC keys (temp up, mode, ...)."""
    categories = client.get("/api/match/categories").json()
    ac = next(c for c in categories if c["name"] == "Air Conditioner")
    brand = client.get(f"/api/match/brands?category_id={ac['id']}").json()[0]
    candidate = client.get(
        f"/api/match/candidates?category_id={ac['id']}&brand_id={brand['id']}"
    ).json()[0]

    with patch("backend.mqtt.MQTTManager.send_ir_code", new=AsyncMock()) as send:
        response = client.post(
            "/api/match/test",
            json={
                "category_id": ac["id"],
                "brand_id": brand["id"],
                "remote_id": candidate["remote_id"],
                "target": ["s11:ir_tx"],
                "power": 1,
                "mode": 1,
                "temperature": 26,
                "wind_speed": 2,
                "ud_wind_mode": 0,
                "function_id": 3,  # temp up
            },
        )

    assert response.status_code == 200
    body = response.json()
    assert body["sent"] is True
    assert body["function_id"] == 3
    assert body["state"]["temperature"] == 26
    assert body["state"]["wind_speed"] == 2
    assert send.await_count == 1
    assert send.await_args.args[0]["payload"]["timings"]


def test_match_test_rejects_bad_remote(client: TestClient):
    response = client.post(
        "/api/match/test",
        json={"category_id": 9999, "brand_id": "nope", "remote_id": "nope"},
    )
    assert response.status_code in (422, 503)
