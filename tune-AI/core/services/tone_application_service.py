"""Apply detected musical keys to the panel's internal control protocol."""

from core.constants import MIC1_CC, MIC2_CC


class ToneApplicationService:
    """Translate tone results into canonical labels and internal MIDI CC state."""

    def __init__(self, tone_service, midi, key_signature_service):
        self.tone_service = tone_service
        self.midi = midi
        self.key_signature_service = key_signature_service

    def detect_from_file(self, file_path: str) -> dict:
        result = self.tone_service.detect_tone_from_file(file_path)
        key = self.key_signature_service.normalize(result["key"], result["scale"])
        result["key"] = key
        result["label"] = f"{key} {result['scale']}"
        return result

    def apply_detected(self, tone_result: dict) -> dict:
        key_cc_value = self._enum_index_to_cc(int(tone_result["key_index"]), 12)
        scale_index = 1 if str(tone_result["scale"]).strip().lower() == "minor" else 0
        scale_cc_value = self._enum_index_to_cc(scale_index, len(self.key_signature_service.scale_options))
        targets = []
        for mic_name, cc_map in (("MIC 1", MIC1_CC), ("MIC 2", MIC2_CC)):
            self.midi.send_cc(cc_map["at_key"], key_cc_value)
            self.midi.send_cc(cc_map["at_scale"], scale_cc_value)
            targets.append({"mic": mic_name, "key_cc": cc_map["at_key"], "scale_cc": cc_map["at_scale"]})
        return {
            "key_cc_value": key_cc_value,
            "scale_cc_value": scale_cc_value,
            "scale_index": scale_index,
            "targets": targets,
        }

    def apply_manual(self, key_name: str, scale_name: str) -> dict:
        scale_name = scale_name.strip()
        key = self.key_signature_service.normalize(key_name, scale_name)
        return self.apply_detected({
            "key": key,
            "key_index": self.key_signature_service.key_index(key_name),
            "scale": scale_name,
            "label": f"{key} {scale_name}",
        })

    def apply_realtime(self, key_name: str, scale_name: str) -> dict:
        key = self.key_signature_service.normalize(key_name, scale_name)
        result = {
            "key": key,
            "key_index": self.key_signature_service.key_index(key),
            "scale": scale_name,
            "label": f"{key} {scale_name}",
        }
        result["applied"] = self.apply_detected(result)
        return result

    @staticmethod
    def _enum_index_to_cc(index: int, total_items: int) -> int:
        if total_items <= 1:
            return 0
        return int(round(max(0, min(total_items - 1, index)) * 127 / (total_items - 1)))
