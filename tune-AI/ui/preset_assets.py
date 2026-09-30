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


PRESET_CARD_ASSETS = {
    "LOFI": _preset_assets("lofi", "lofi"),
    "NHẠC TRẺ": _preset_assets("nhac_tre", "nhac_tre"),
    "BOLERO": _preset_assets("bolero", "bolero"),
    "REMIX": _preset_assets("remix", "remix"),
}
