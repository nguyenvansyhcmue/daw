"""Named asset locations for the four singing-mode preset cards."""

from dataclasses import dataclass
from pathlib import Path

from core.constants import ASSETS_DIR


@dataclass(frozen=True)
class PresetCardAssets:
    """The independent background and icon used by one preset card."""

    background: Path
    icon: Path


def _preset_assets(folder: str, stem: str) -> PresetCardAssets:
    directory = ASSETS_DIR / "presets" / folder
    return PresetCardAssets(
        background=directory / f"{stem}_background.png",
        icon=directory / f"{stem}_icon.png",
    )


def _analog_tile(filename: str) -> PresetCardAssets:
    tile = ASSETS_DIR / "analog" / "05_MODE_TILES" / filename
    return PresetCardAssets(background=tile, icon=Path())


PRESET_CARD_ASSETS = {
    "LOFI": _analog_tile("TILE_LOFI.png"),
    "NHẠC TRẺ": _preset_assets("nhac_tre", "nhac_tre"),
    "BOLERO": _analog_tile("TILE_BOLERO.png"),
    "REMIX": _analog_tile("TILE_REMIX.png"),
}

# Asset-pack tile for the Vietnamese mode label (keep the legacy fallback key
# above for older saved layouts).
PRESET_CARD_ASSETS["NHẠC TRẺ"] = _analog_tile("TILE_NHAC_TRE.png")
