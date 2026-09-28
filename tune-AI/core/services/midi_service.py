import mido
from core.constants import MIDI_PORT_HINT


class MidiService:
    def __init__(self, port_hint: str = MIDI_PORT_HINT):
        self.port_hint = port_hint
        self.port = None

    def open(self):
        names = mido.get_output_names()
        for name in names:
            if self.port_hint.lower() in name.lower():
                self.port = mido.open_output(name)
                return name
        raise RuntimeError(f"Không tìm thấy cổng MIDI chứa: {self.port_hint}")

    def ensure_open(self):
        if self.port is None:
            self.open()

    def send_cc(self, cc: int, value: int, channel: int = 0):
        self.ensure_open()
        value = max(0, min(127, int(value)))
        msg = mido.Message(
            "control_change",
            channel=channel,
            control=cc,
            value=value
        )
        self.port.send(msg)

    def close(self):
        if self.port is not None:
            self.port.close()
            self.port = None