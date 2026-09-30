"""Shared visual tokens for the Analog Future presentation layer."""

from dataclasses import dataclass
from pathlib import Path

from PyQt6.QtGui import QColor

from core.constants import ASSETS_DIR


@dataclass(frozen=True)
class AnalogTokens:
    reference_width: int = 1080
    reference_height: int = 684
    chassis_black: str = "#0B0C0D"
    panel_graphite: str = "#151719"
    warm_steel: str = "#8C765E"
    ivory_text: str = "#E8D9C2"
    amber_lcd: str = "#E49A3A"
    amber_low_glow: str = "#A65A1C"
    exit_red: str = "#B24A45"
    panel_radius: int = 8
    panel_shadow_alpha: int = 108
    edge_highlight_alpha: int = 46


TOKENS = AnalogTokens()
ANALOG_ASSETS = ASSETS_DIR / "analog"


def asset(*parts: str) -> Path:
    return ANALOG_ASSETS.joinpath(*parts)


def color(value: str) -> QColor:
    return QColor(value)
