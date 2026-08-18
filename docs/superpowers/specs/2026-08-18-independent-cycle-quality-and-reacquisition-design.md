# Independent-cycle quality validation and reacquisition design

## Problem

The independent-cycle processor currently treats any low-to-high threshold transition separated by at least half a nominal period as a valid boundary. It then accepts every interval between boundaries from `0.5T` to `1.5T` and stretches it to one nominal period. Low-amplitude measurements can contain a short threshold-crossing spike, producing intervals such as `0.54T` and `1.39T`. These intervals pass the current count check, are resampled as complete cycles, and cause an artificial fit with a large or capped `tau`.

The observed data uses 20 MHz sampling at 50 Hz, so `T=400000` points. The 79 mV samples have regular intervals near `T`; the 72 mV and 65 mV samples contain short and long false intervals. The ADC step is approximately 2.441 mV, making transient threshold crossings more likely at the lower signal levels.

## Goals

1. Count only complete, quality-checked periods toward the requested average count `N`.
2. Reject transient threshold crossings without creating a phase boundary.
3. Reject intervals that are materially shorter or longer than one nominal period.
4. Reject cycles with implausible amplitude, baseline, or shape after enough good cycles exist to form a template.
5. Continue searching in the current acquisition after rejecting a cycle.
6. Start a new independent acquisition when the current buffer cannot provide enough valid cycles.
7. Never concatenate raw samples across independent acquisitions. Only normalized, validated cycles may be combined for averaging.
8. Expose enough counters and error information to distinguish insufficient data from rejected cycle quality.

## Non-goals

- Changing the legacy threshold-accumulation processing mode.
- Changing the exponential fitting equation or its optimization method.
- Silently accepting a lower number of cycles than requested.
- Stitching raw samples or phases between independent acquisitions.
- Hiding a failed acquisition behind a fallback fit.

## Definitions and fixed formulas

For sampling frequency `Fs` and emitting frequency `Fe`:

```text
T = round(Fs / Fe)
```

For the current 50 Hz, 20 MHz case, `T=400000` points and the sample interval is `0.05 us`.

### Robust threshold range

Use robust percentiles over the acquisition buffer instead of single global extrema:

```text
Vmin = P1(samples)
Vmax = P99(samples)
A = Vmax - Vmin
L = Vmin + 0.10 * A
U = Vmin + 0.40 * A
```

The existing minimum raw span check remains a separate acquisition-level check. The robust threshold range must be non-empty and finite.

### Threshold confirmation

Set:

```text
K = max(32, round(0.00005 * T))
```

For `T=400000`, `K=32` points. A low-state confirmation requires at least 75% of the K-point window to satisfy `x <= L`. A high-state confirmation requires at least 75% of the K-point window to satisfy `x >= U`.

A single sample, or a short excursion that immediately falls back below `U`, is noise and does not create a boundary. A confirmed boundary is placed at the first sample of the confirmed high window.

### Period-length validation

For consecutive confirmed boundaries `s_i` and `s_j`:

```text
D = s_j - s_i
```

The interval is a complete cycle only if:

```text
0.90 * T <= D <= 1.10 * T
```

An early candidate (`D < 0.90T`) is discarded while retaining the previous accepted boundary. A late candidate (`D > 1.10T`) cannot be resampled as one cycle; the interval is discarded and the detector re-anchors at the late candidate so that subsequent cycles can be searched independently.

If a candidate is approximately a multiple of `T`, it indicates a missed boundary, not a valid long cycle. The skipped interval is not counted toward `N`.

### Amplitude validation

For a candidate interval, use robust cycle amplitude:

```text
A_i = P99(cycle) - P1(cycle)
```

After at least three valid cycles establish a reference:

```text
A_ref = median(A_1, A_2, ...)
```

Accept later cycles only if:

```text
0.70 * A_ref <= A_i <= 1.30 * A_ref
```

Using percentiles prevents one quantization spike from determining amplitude.

### Baseline validation

For each cycle, calculate the baseline from the 150 points immediately preceding the 300-point pre-boundary window:

```text
B_i = median(samples[s_i - 300 ... s_i - 150])
```

After a reference baseline is available:

```text
B_ref = median(B_1, B_2, ...)
baseline_error = abs(B_i - B_ref)
```

Accept only if:

```text
baseline_error <= max(0.005 V, 0.20 * A_ref)
```

The pre-boundary window must be fully available; otherwise the cycle is invalid.

### Shape validation

The first three cycles use boundary, period, amplitude, and baseline checks only. After that, the normalized cycle becomes a template candidate. For a cycle `y_i` and template `y_ref`, calculate:

```text
RMSE_i = sqrt(sum((y_i[k] - y_ref[k])^2) / M)
shape_error = RMSE_i / A_ref
rho_i = correlation(y_i, y_ref)
```

Accept only if:

```text
shape_error <= 0.15
rho_i >= 0.90
```

The template is updated only with accepted cycles, using a running average or median-based robust update. Rejected cycles never affect the template.

## Acquisition and retry flow

For a requested average count `N`:

1. Acquire one independent buffer containing at least `N+1` nominal periods.
2. Detect confirmed boundaries and validate intervals.
3. Keep only validated cycles.
4. If `N` valid cycles are available, use exactly `N` for averaging and fitting.
5. If the buffer has remaining samples, continue searching for additional valid cycles.
6. If the buffer is exhausted before reaching `N`, start another independent acquisition.
7. Do not combine raw buffers. Combine only validated, independently normalized cycles.
8. Stop after a configurable maximum of three independent acquisitions. If fewer than `N` valid cycles exist, return an explicit insufficient-valid-waveforms error.

The acquisition counters must distinguish:

- requested waveforms;
- accepted waveforms;
- discarded waveforms;
- rejected short/long intervals;
- rejected threshold confirmations;
- rejected amplitude/baseline cycles;
- rejected shape cycles;
- number of independent batches;
- number of retries.

## Error behavior

`success=true` is allowed only when exactly `N` valid cycles have been collected. A low fit loss must not override failed cycle validation. If the maximum retry count is reached, processing fails with a machine-readable category indicating insufficient valid independent cycles.

## Acceptance criteria

1. The 79 mV 16-cycle and 32-cycle files retain regular periods and produce the existing normal result within the existing tolerance.
2. The 72 mV 16-cycle and 32-cycle files reject approximately `0.54T` and `1.39T` intervals; those intervals are never stretched into cycles.
3. The 65 mV 32-cycle file follows the same rejection path and does not produce a capped `tau` from a malformed averaged wave.
4. A synthetic one-point or short high-threshold spike does not create a boundary.
5. A valid cycle with a period in `[0.90T,1.10T]` is accepted.
6. A cycle with period outside that range is rejected and does not contribute to `N`.
7. A second independent acquisition can supply cycles missing from the first acquisition without raw phase stitching.
8. Processing fails explicitly when the retry limit is exhausted before collecting `N` valid cycles.
9. Legacy mode behavior and existing KDM3000 mode selection remain unchanged.

