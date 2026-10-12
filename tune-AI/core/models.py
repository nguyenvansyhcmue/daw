"""Small domain models shared by core services."""

from dataclasses import dataclass


@dataclass
class ModePreset:
    reverb_short: int = 50
    reverb_long: int = 50
    echo: int = 50
    tune: int = 200
