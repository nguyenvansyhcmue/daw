"""
Debug V1+ enhanced (auto detect path).
Goi truc tiep detect_from_youtube de kiem tra ket qua + thoi gian.

Usage:
    python debug_auto.py <youtube_url> [expected_label]
"""

import sys
import time

try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

sys.path.insert(0, ".")

from core.services.tone_service import detect_from_youtube


def main():
    if len(sys.argv) < 2:
        print("Usage: python debug_auto.py <youtube_url> [expected_label]")
        sys.exit(1)

    url = sys.argv[1]
    expected = sys.argv[2] if len(sys.argv) > 2 else ""

    print("=" * 78)
    print(f"AUTO DETECT V1+ ENHANCED: {url}")
    if expected:
        print(f"GROUND TRUTH: {expected}")
    print("=" * 78)

    t0 = time.time()
    result = detect_from_youtube(url, verbose=False)
    elapsed = time.time() - t0

    print(f"\nTitle:    {result.get('title', '')}")
    print(f"Duration: {result.get('duration', 0)}s")
    print(f"Elapsed:  {elapsed:.1f}s (auto detect path)")
    print(f"\n>>> RESULT: {result['label']}  (confidence {result.get('confidence', 0):.3f})")

    if expected:
        if result["label"].strip().lower() == expected.strip().lower():
            print(f"    [OK] Khop ground truth '{expected}'")
        else:
            print(f"    [FAIL] Sai. Ground truth: '{expected}'")

    print("\nTOP 8 ranked:")
    for i, item in enumerate(result.get("ranked", []), 1):
        if isinstance(item, (list, tuple)):
            label, score = item[0], item[1]
        else:
            label, score = str(item), 0.0
        marker = "  <-- GROUND TRUTH" if expected and label.strip().lower() == expected.strip().lower() else ""
        print(f"  {i}. {label:<14} {score:.4f}{marker}")

    print("=" * 78)


if __name__ == "__main__":
    main()
