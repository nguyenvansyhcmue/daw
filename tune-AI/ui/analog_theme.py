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


@dataclass(frozen=True)
class ReferenceBounds:
    """A rectangle in the unscaled 1080×684 chassis coordinate space."""

    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True)
class AnalogReferenceLayout:
    """Single source of truth for live controls over the chassis artwork.

    The reference PNG owns fixed hardware: bezels, labels, scales and rails.
    These bounds therefore only cover the data-changing interiors or hit areas.
    """

    tone_readout: ReferenceBounds = ReferenceBounds(275, 125, 215, 96)
    # The reference meter's two physical columns occupy this exact well.
    # Keeping live geometry here prevents a second, offset meter layer.
    input_meter: ReferenceBounds = ReferenceBounds(183, 120, 28, 118)
    # The waveform engraved in the top nameplate is replaced at runtime by a
    # live, level-driven display.  Keep its bounds separate from the artwork
    # so the panel bezel and typography remain untouched.
    header_waveform: ReferenceBounds = ReferenceBounds(350, 31, 235, 38)
    signal_rail: ReferenceBounds = ReferenceBounds(510, 219, 290, 16)
    tone_action_y: int = 118
    tone_action_size: int = 88
    tone_actions: tuple[int, int, int] = (520, 611, 702)
    tone_shift_minus: ReferenceBounds = ReferenceBounds(835, 158, 48, 58)
    tone_shift_plus: ReferenceBounds = ReferenceBounds(998, 158, 48, 58)
    # Covers only the LCD glass, never its bezel or the fixed semitone scale.
    tone_shift_display: ReferenceBounds = ReferenceBounds(892, 155, 100, 64)
    # Fixed Lofi/Bolero artwork and live Nhạc Trẻ/Remix replacement art share
    # these bounds, preserving the physical gaps in the chassis.
    lofi_tile: ReferenceBounds = ReferenceBounds(38, 285, 232, 84)
    nhac_tre_tile: ReferenceBounds = ReferenceBounds(280, 285, 248, 84)
    bolero_tile: ReferenceBounds = ReferenceBounds(538, 285, 247, 84)
    remix_tile: ReferenceBounds = ReferenceBounds(795, 285, 246, 84)
    effect_panel: ReferenceBounds = ReferenceBounds(20, 378, 516, 180)
    volume_panel: ReferenceBounds = ReferenceBounds(544, 378, 516, 180)
    # A knob must live strictly between its caption (ends at y=425) and its
    # numeric readout (begins at y=526); these centers serve both panels.
    knob_size: int = 100
    knob_y: int = 426
    knob_centers: tuple[int, int, int, int, int, int] = (106, 278, 450, 630, 802, 974)
    knob_readout_y: int = 526
    knob_readout_width: int = 60
    knob_readout_height: int = 23


REFERENCE_LAYOUT = AnalogReferenceLayout()
ANALOG_ASSETS = ASSETS_DIR / "analog"


def asset(*parts: str) -> Path:
    return ANALOG_ASSETS.joinpath(*parts)


def color(value: str) -> QColor:
    return QColor(value)
