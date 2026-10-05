"""Tests for the Kookong IRDB provider.

These are integration-flavoured: they need the bundled ``kkoffline.db`` and the
native ``libkksdk_host.so``.  They skip automatically when either is missing, so
they stay green on machines without the Kookong artifacts.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from backend.providers.kookong import KookongProvider, _signed_timings, _slug


DB_CANDIDATES = [
    Path(os.environ.get("KKOOKONG_DB", "/data/kkoffline.db")),
    Path("/usr/local/share/kookong/kkoffline.db"),
]
LIB_CANDIDATES = [
    Path(os.environ.get("KKOOKONG_LIB", "/usr/lib/libkksdk_host.so")),
    Path("/usr/local/lib/libkksdk_host.so"),
]


def _artifacts_available() -> bool:
    db = any(p.exists() for p in DB_CANDIDATES)
    lib = any(p.exists() for p in LIB_CANDIDATES)
    return db and lib


requires_artifacts = pytest.mark.skipif(
    not _artifacts_available(),
    reason="kkoffline.db and/or libkksdk_host.so not available",
)


def test_signed_timings_convention():
    # Kookong yields alternating mark/space durations, all positive.
    assert _signed_timings([100, 200, 300, 400]) == [100, -200, 300, -400]


def test_slug_is_filesystem_safe():
    assert _slug("Mitsubishi Electric") == "Mitsubishi_Electric"
    assert _slug("TV / Audio") == "TV_Audio"
    assert _slug("  ") == "unknown"


def test_provider_metadata():
    provider = KookongProvider()
    assert provider.id == "kookong"
    assert provider.name


@requires_artifacts
def test_convert_produces_raw_buttons():
    provider = KookongProvider()
    provider._brands_raw = "Samsung"
    provider._max_remotes = 3
    provider._max_per_brand = 2
    provider._max_keys = 10
    provider._min_keys = 1
    provider._resolve_paths()

    remotes = provider.convert(provider._db_path.parent)

    assert remotes, "expected at least one remote"
    for remote in remotes:
        assert remote["provider"] == "kookong"
        assert remote["path"].startswith("kookong/")
        for button in remote["buttons"]:
            code = button["code"]
            assert code["protocol"] == "raw"
            timings = code["payload"]["timings"]
            assert timings, "raw button must carry timings"
            # IR2MQTT/Flipper convention: positive mark, negative space,
            # starting with a mark.
            assert timings[0] > 0
            for index, value in enumerate(timings):
                assert (value > 0) if index % 2 == 0 else (value < 0)


@requires_artifacts
def test_convert_reports_stats():
    provider = KookongProvider()
    provider._brands_raw = "Samsung"
    provider._max_remotes = 2
    provider._max_per_brand = 1
    provider._max_keys = 5
    provider._min_keys = 1
    provider._resolve_paths()

    provider.convert(provider._db_path.parent)

    stats = provider.last_convert_stats
    assert stats["imported"] >= 0
    assert "skip_reasons" in stats
