import csv
import math
from pathlib import Path

import numpy as np


INPUT_CSV = Path("industry_data_cleaned_2.csv")
OUTPUT_CSV = Path("industry_features.csv")

# With only 58 points per class, a moderate window keeps local patterns while
# still producing enough training samples. Change these two values for ablation.
WINDOW_SIZE = 16
STEP_SIZE = 2


def read_series_table(input_path: Path):
    """Read the 7-row time-series table and ignore empty time columns."""
    series = []

    with input_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)

        if not header or header[-1] != "类别":
            raise ValueError("CSV最后一列应为：类别")

        for row in reader:
            if not row:
                continue

            label = row[-1].strip()
            values = []

            for cell in row[:-1]:
                cell = cell.strip()
                if cell == "":
                    continue
                values.append(float(cell))

            if values:
                series.append((label, np.array(values, dtype=float)))

    return series


def safe_skewness(x: np.ndarray) -> float:
    mean = np.mean(x)
    std = np.std(x)
    if std == 0:
        return 0.0
    return float(np.mean(((x - mean) / std) ** 3))


def safe_kurtosis(x: np.ndarray) -> float:
    mean = np.mean(x)
    std = np.std(x)
    if std == 0:
        return 0.0
    return float(np.mean(((x - mean) / std) ** 4) - 3.0)


def zero_crossing_rate(x: np.ndarray) -> float:
    if len(x) < 2:
        return 0.0
    signs = np.sign(x)
    crossings = np.sum(signs[1:] * signs[:-1] < 0)
    return float(crossings / (len(x) - 1))


def slope(x: np.ndarray) -> float:
    if len(x) < 2:
        return 0.0
    t = np.arange(len(x), dtype=float)
    t = t - np.mean(t)
    denom = np.sum(t * t)
    if denom == 0:
        return 0.0
    return float(np.sum(t * (x - np.mean(x))) / denom)


def longest_run_ratio(mask: np.ndarray) -> float:
    longest = 0
    current = 0

    for flag in mask:
        if flag:
            current += 1
            longest = max(longest, current)
        else:
            current = 0

    return float(longest / len(mask)) if len(mask) else 0.0


def spectral_features(x: np.ndarray):
    centered = x - np.mean(x)
    spectrum = np.fft.rfft(centered)
    power = np.abs(spectrum) ** 2

    if len(power) <= 1 or np.sum(power[1:]) == 0:
        return {
            "fft_low_ratio": 0.0,
            "fft_high_ratio": 0.0,
            "fft_dominant_ratio": 0.0,
            "fft_spectral_centroid": 0.0,
        }

    non_dc_power = power[1:]
    total = np.sum(non_dc_power)
    freqs = np.arange(1, len(power), dtype=float)

    split = max(1, len(non_dc_power) // 2)
    low_power = np.sum(non_dc_power[:split])
    high_power = np.sum(non_dc_power[split:])

    return {
        "fft_low_ratio": float(low_power / total),
        "fft_high_ratio": float(high_power / total),
        "fft_dominant_ratio": float(np.max(non_dc_power) / total),
        "fft_spectral_centroid": float(np.sum(freqs * non_dc_power) / total),
    }


def extract_features(x: np.ndarray):
    diff = np.diff(x)
    abs_x = np.abs(x)
    abs_diff = np.abs(diff)

    mean = np.mean(x)
    std = np.std(x)
    rms = math.sqrt(np.mean(x * x))
    peak_abs = np.max(abs_x)
    q25, q50, q75 = np.percentile(x, [25, 50, 75])
    abs_q75 = np.percentile(abs_x, 75)

    if len(diff) > 0:
        diff_mean_abs = np.mean(abs_diff)
        diff_std = np.std(diff)
        diff_max_abs = np.max(abs_diff)
        diff_rms = math.sqrt(np.mean(diff * diff))
        large_jump_ratio = np.mean(abs_diff > (diff_mean_abs + diff_std))
    else:
        diff_mean_abs = 0.0
        diff_std = 0.0
        diff_max_abs = 0.0
        diff_rms = 0.0
        large_jump_ratio = 0.0

    features = {
        # Amplitude and distribution.
        "mean": float(mean),
        "std": float(std),
        "min": float(np.min(x)),
        "max": float(np.max(x)),
        "median": float(q50),
        "q25": float(q25),
        "q75": float(q75),
        "iqr": float(q75 - q25),
        "range": float(np.max(x) - np.min(x)),
        "rms": float(rms),
        "mean_abs": float(np.mean(abs_x)),
        "peak_abs": float(peak_abs),
        "crest_factor": float(peak_abs / rms) if rms != 0 else 0.0,
        "skewness": safe_skewness(x),
        "kurtosis": safe_kurtosis(x),
        # Sign and direction. Useful because forward/backward collision may
        # differ mainly by current polarity and positive/negative persistence.
        "positive_ratio": float(np.mean(x > 0)),
        "negative_ratio": float(np.mean(x < 0)),
        "zero_crossing_rate": zero_crossing_rate(x),
        "longest_positive_run": longest_run_ratio(x > 0),
        "longest_negative_run": longest_run_ratio(x < 0),
        # Local dynamics and sudden-change information.
        "slope": slope(x),
        "first_to_last": float(x[-1] - x[0]),
        "diff_mean_abs": float(diff_mean_abs),
        "diff_std": float(diff_std),
        "diff_max_abs": float(diff_max_abs),
        "diff_rms": float(diff_rms),
        "large_jump_ratio": float(large_jump_ratio),
        # Robust high-amplitude indicator, less sensitive than using max only.
        "abs_q75": float(abs_q75),
    }

    features.update(spectral_features(x))
    return features


def build_feature_table(series, window_size: int, step_size: int):
    rows = []

    for label, values in series:
        if len(values) < window_size:
            continue

        for start in range(0, len(values) - window_size + 1, step_size):
            window = values[start : start + window_size]
            features = extract_features(window)
            features["window_start"] = start
            features["window_end"] = start + window_size - 1
            features["label"] = label
            rows.append(features)

    return rows


def write_feature_table(rows, output_path: Path):
    if not rows:
        raise ValueError("没有生成任何特征样本，请检查窗口长度是否过大")

    feature_columns = [
        "mean",
        "std",
        "min",
        "max",
        "median",
        "q25",
        "q75",
        "iqr",
        "range",
        "rms",
        "mean_abs",
        "peak_abs",
        "crest_factor",
        "skewness",
        "kurtosis",
        "positive_ratio",
        "negative_ratio",
        "zero_crossing_rate",
        "longest_positive_run",
        "longest_negative_run",
        "slope",
        "first_to_last",
        "diff_mean_abs",
        "diff_std",
        "diff_max_abs",
        "diff_rms",
        "large_jump_ratio",
        "abs_q75",
        "fft_low_ratio",
        "fft_high_ratio",
        "fft_dominant_ratio",
        "fft_spectral_centroid",
        "window_start",
        "window_end",
        "label",
    ]

    with output_path.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=feature_columns)
        writer.writeheader()
        writer.writerows(rows)


def main():
    series = read_series_table(INPUT_CSV)
    rows = build_feature_table(series, WINDOW_SIZE, STEP_SIZE)
    write_feature_table(rows, OUTPUT_CSV)

    labels = sorted({row["label"] for row in rows})
    print(f"读取类别数: {len(series)}")
    print(f"窗口长度: {WINDOW_SIZE}, 步长: {STEP_SIZE}")
    print(f"生成样本数: {len(rows)}")
    print(f"输出文件: {OUTPUT_CSV}")
    for label in labels:
        count = sum(1 for row in rows if row["label"] == label)
        print(f"{label}: {count} 个窗口样本")


if __name__ == "__main__":
    main()
