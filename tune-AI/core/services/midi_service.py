class MidiService:
    """In-process control state for StudioForge/Tune-AI.

    Tune-AI used to send CC messages through an external virtual MIDI port.
    StudioForge is now the DAW, so the panel must never discover,
    open, or send to an external MIDI port.  We retain the small service API so
    existing UI controls continue to work while the future native DAW bridge
    can consume ``last_values`` directly.
    """

    def __init__(self):
        self.last_values: dict[tuple[int, int], int] = {}
        self.is_open = False

    def open(self):
        self.is_open = True
        return "StudioForge internal controls"

    def ensure_open(self):
        if not self.is_open:
            self.open()

    def send_cc(self, cc: int, value: int, channel: int = 0):
        self.ensure_open()
        value = max(0, min(127, int(value)))
        self.last_values[(int(channel), int(cc))] = value

    def close(self):
        self.is_open = False
