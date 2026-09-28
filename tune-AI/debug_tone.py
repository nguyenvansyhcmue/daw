"""
Debug script: phân tích chi tiết FIX TONE V2 cho 1 YouTube URL.
In ra breakdown từng component đã normalize cho top 10 candidate,
giúp xác định component nào đang kéo lệch sang relative major.

Usage:
    python debug_tone.py <youtube_url> [expected_label]
    python debug_tone.py "https://youtu.be/xxx" "E Minor"
"""

import sys
import time

# Bat buoc UTF-8 stdout cho console Windows (de in title YouTube tieng Viet).
try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

sys.path.insert(0, ".")

from core.services.tone_service import (
    clean_youtube_url,
    get_stream_url,
    choose_fix_segments,
    stream_to_numpy,
    _analyze_fix_audio_segment,
    _profile_score,
    _tonal_center_raw,
    _cadence_raw,
    _bass_root_raw,
    _degree_function_raw,
    _candidate_pool_24,
    _minmax_component,
    _load_final_outro_chroma,
)


WEIGHTS = {
    "global_chroma": 0.08,
    "intro": 0.06,
    "main_segments": 0.14,
    "tonal_center": 0.20,
    "cadence": 0.10,
    "bass_root": 0.22,
    "degree_function": 0.08,
    "final_outro": 0.06,
    "relative": 0.04,
    "history": 0.02,
}


def debug_url(youtube_url: str, expected_label: str = ""):
    print("=" * 78)
    print(f"DEBUG TONE: {youtube_url}")
    if expected_label:
        print(f"GROUND TRUTH: {expected_label}")
    print("=" * 78)

    t0 = time.time()
    clean_url = clean_youtube_url(youtube_url)
    print("[1] Lay stream URL ...")
    stream_url, title, duration = get_stream_url(clean_url)
    print(f"    Title:    {title}")
    print(f"    Duration: {duration}s")

    segments = choose_fix_segments(duration)
    print(f"\n[2] Phan tich {len(segments)} segments:")
    for s in segments:
        print(f"    role={s['role']:<11} start={s['start']:>4}s  weight={s['weight']:.3f}")

    print("\n[3] Tinh chroma + raw scores ...")
    segment_infos = []
    for seg in segments:
        audio = stream_to_numpy(
            stream_url=stream_url,
            seconds=int(seg["seconds"]),
            start_at=int(seg["start"]),
        )
        info = _analyze_fix_audio_segment(audio, seg["role"], seg["weight"])
        segment_infos.append(info)
        eff = info.get("effective_weight", seg["weight"])
        hr = info.get("harmonic_ratio", 1.0)
        print(
            f"    {seg['role']:<11} harmonic_ratio={hr:.3f}  effective_weight={eff:.3f}"
        )
        print(f"        top3 chroma raw: {info['top8'][:3]}")

    # Tai 8s cuoi BAI THAT cho component final_outro.
    final_chroma = _load_final_outro_chroma(stream_url, duration, seconds=8)
    print(f"\n    final_outro chroma: {'loaded' if final_chroma is not None else 'None (skipped)'}")

    # Reproduce logic deep_balanced_vote_fix de in full breakdown.
    candidates = _candidate_pool_24()
    labels = [c["label"] for c in candidates]

    raw = {k: {l: 0.0 for l in labels} for k in WEIGHTS}
    # final_outro raw: ngoai loop segment, tinh truc tiep tu final_chroma neu co.
    if final_chroma is not None:
        for cand in candidates:
            raw["final_outro"][cand["label"]] = _cadence_raw(cand, final_chroma)

    intro_w = main_w = 0.0
    for seg in segment_infos:
        role = seg["role"]
        weight = float(seg.get("effective_weight", seg.get("weight", 1.0)))
        chroma = seg["chroma"]
        bass_chroma = seg["bass_chroma"]
        cadence_chroma = seg["cadence_chroma"]
        is_intro = role.startswith("intro")
        if is_intro:
            intro_w += weight
        else:
            main_w += weight
        for cand in candidates:
            l = cand["label"]
            p = _profile_score(cand, chroma)
            raw["global_chroma"][l] += p * weight
            if is_intro:
                raw["intro"][l] += p * weight
            else:
                raw["main_segments"][l] += p * weight
            raw["tonal_center"][l] += _tonal_center_raw(cand, chroma) * weight
            raw["cadence"][l] += _cadence_raw(cand, cadence_chroma) * weight
            raw["bass_root"][l] += _bass_root_raw(cand, bass_chroma) * weight
            raw["degree_function"][l] += _degree_function_raw(cand, chroma) * weight

    if intro_w > 1e-9:
        for l in labels:
            raw["intro"][l] /= intro_w
    if main_w > 1e-9:
        for l in labels:
            raw["main_segments"][l] /= main_w

    norm = {k: _minmax_component(v) for k, v in raw.items()}

    final = {}
    for l in labels:
        final[l] = sum(WEIGHTS[k] * norm[k].get(l, 0.0) for k in WEIGHTS)

    ranked = sorted(final.items(), key=lambda x: x[1], reverse=True)

    print("\n[4] TOP 10 FINAL RANKED (component scores normalized 0..1):")
    cols = ["global", "intro", "main", "tonal", "cadnc", "bass", "deg", "outro"]
    keys = [
        "global_chroma",
        "intro",
        "main_segments",
        "tonal_center",
        "cadence",
        "bass_root",
        "degree_function",
        "final_outro",
    ]
    header = f"    {'#':<3}{'Label':<13}{'Final':<8}"
    for c in cols:
        header += f"{c:<7}"
    print(header)
    print("    " + "-" * (3 + 13 + 8 + 7 * len(cols)))
    for i, (label, score) in enumerate(ranked[:10], 1):
        marker = ""
        if expected_label and label.strip().lower() == expected_label.strip().lower():
            marker = "  <-- GROUND TRUTH"
        line = f"    {i:<3}{label:<13}{score:.4f}  "
        for k in keys:
            line += f"{norm[k].get(label, 0.0):<7.3f}"
        print(line + marker)

    # Vi tri ground truth.
    if expected_label:
        gt_lower = expected_label.strip().lower()
        for i, (label, score) in enumerate(ranked, 1):
            if label.strip().lower() == gt_lower:
                gap = ranked[0][1] - score
                print(
                    f"\n[5] Ground truth '{expected_label}' xep hang #{i}/24 "
                    f"voi score {score:.4f} (kem top1 {gap:.4f})"
                )
                # Phan tich diem yeu — component nao GT thua best?
                best_label = ranked[0][0]
                if best_label != label:
                    print(f"\n[6] So sanh '{label}' vs '{best_label}' theo tung component:")
                    print(
                        f"    {'Component':<18}{'GT':<8}{'BEST':<8}{'Gap':<8}{'Weight':<8}{'Contrib':<10}"
                    )
                    print("    " + "-" * 60)
                    total_contrib = 0.0
                    for k in WEIGHTS:
                        gt_v = norm[k].get(label, 0.0)
                        best_v = norm[k].get(best_label, 0.0)
                        gap_v = best_v - gt_v
                        contrib = gap_v * WEIGHTS[k]
                        total_contrib += contrib
                        flag = " <-- KEY ISSUE" if abs(contrib) >= 0.01 else ""
                        print(
                            f"    {k:<18}{gt_v:<8.3f}{best_v:<8.3f}"
                            f"{gap_v:+<8.3f}{WEIGHTS[k]:<8.2f}{contrib:+<10.4f}{flag}"
                        )
                    print(f"    {'TOTAL':<18}{'':<8}{'':<8}{'':<8}{'':<8}{total_contrib:+<10.4f}")
                break

    elapsed = time.time() - t0
    print(f"\nElapsed: {elapsed:.1f}s")
    print("=" * 78)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python debug_tone.py <youtube_url> [expected_label]")
        sys.exit(1)
    url = sys.argv[1]
    expected = sys.argv[2] if len(sys.argv) > 2 else ""
    debug_url(url, expected)
