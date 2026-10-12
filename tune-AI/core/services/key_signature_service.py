"""Pure key and scale normalization rules for vocal-tone workflows."""


class KeySignatureService:
    """Provide one canonical spelling for every supported pitch class."""

    SCALE_OPTIONS = (
        "Major", "Minor", "Chromatic", "Ling Lun", "Scholar's Lute", "Greek Diatonic",
        "Greek Chromatic", "Greek Enharmonic", "Pythagorean", "Just (Major)", "Just (Minor)",
        "Meantone Chromatic", "Werckmeister I (III)", "Vallotti & Young", "Barnes-Bach", "Indian",
        "Slendro", "Pelog", "Arabic 1", "Arabic 2", "19 Tone", "24 Tone", "31 Tone", "53 Tone",
        "Partch", "Carlos A", "Carlos B", "Carlos G", "Harmonic",
    )
    MAJOR_NOTE_NAMES = ("C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B")
    MINOR_NOTE_NAMES = ("C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B")
    NOTE_TO_INDEX = {
        "C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5,
        "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11,
    }

    @property
    def scale_options(self) -> list[str]:
        return list(self.SCALE_OPTIONS)

    def key_options_for_scale(self, scale_name: str) -> list[str]:
        names = self.MINOR_NOTE_NAMES if str(scale_name or "").strip().lower() == "minor" else self.MAJOR_NOTE_NAMES
        return list(names)

    def normalize(self, key_name: str, scale_name: str) -> str:
        key_name = str(key_name or "").strip()
        index = self.NOTE_TO_INDEX.get(key_name)
        if index is None:
            return key_name
        names = self.MINOR_NOTE_NAMES if str(scale_name or "").strip().lower() == "minor" else self.MAJOR_NOTE_NAMES
        return names[index]

    def key_index(self, key_name: str) -> int:
        key_name = str(key_name or "").strip()
        if key_name not in self.NOTE_TO_INDEX:
            raise ValueError(f"Key không hợp lệ: {key_name}")
        return self.NOTE_TO_INDEX[key_name]
