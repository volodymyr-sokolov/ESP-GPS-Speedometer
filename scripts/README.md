# ESP GPS Speedometer with Auto Timezone — Python Scripts

## Toolchain: Generating Timezone Data

The two Python scripts form a pipeline: GeoJSON download → Arduino headers.

### Prerequisites

```bash
pip install requests shapely pytz tqdm
```

### Step 1 — `convert.py` — Download & Enrich GeoJSON

Downloads the timezone boundary GeoJSON from `timezone-boundary-builder`, simplifies polygon geometry, and adds DST rules + country codes to every feature.

```bash
# Basic usage (defaults: tolerance=0.05°, release=2025b, current year for DST)
python convert.py

# Custom tolerance (lower = more accurate but larger file)
python convert.py --tolerance 0.03 --output timezones_dst.geojson

# Specific release and DST year
python convert.py --release 2025b --year 2026 --output timezones_2026.geojson

# Finer detail for regions with complex borders (e.g. US/Canada)
python convert.py --tolerance 0.02 --output timezones_fine.geojson
```

**Arguments:**

| Argument | Default | Description |
|---|---|---|
| `--tolerance` | `0.05` | Polygon simplification in degrees (~5 km). Lower = more precise, larger file |
| `--output` | `timezones_dst.geojson` | Output filename |
| `--release` | `2025b` | timezone-boundary-builder release tag |
| `--year` | current year | Year used to calculate DST transition rules |

**Output GeoJSON properties per feature:**

```json
{
  "country":          "DE",
  "tzid":             "Europe/Berlin",
  "utc_offset_std":   1.0,
  "utc_offset_dst":   2.0,
  "has_dst":          true,
  "dst_start":        "M3.5.0/02:00",
  "dst_end":          "M10.5.0/03:00",
  "dst_offset_hours": 1.0,
  "abbr_std":         "CET",
  "abbr_dst":         "CEST"
}
```

---

### Step 2 — `compress.py` — Convert GeoJSON → Arduino Headers

Converts the enriched GeoJSON into two C header files ready to be included in an Arduino/ESP32 sketch.

```bash
# Basic usage (reads timezones_dst.geojson, writes timezone_data.h + timezone_lookup.h)
python compress.py

# Explicit input and output paths
python compress.py --input timezones_dst.geojson \
                   --out-data timezone_data.h \
                   --out-lookup timezone_lookup.h

# Tune max points per polygon ring for your target platform
python compress.py --max-pts 200   # ESP32    (~4 MB Flash)
python compress.py --max-pts 120   # ESP8266  (~1 MB Flash)
python compress.py --max-pts 80    # Mega     (~256 KB Flash)
python compress.py --max-pts 35    # UNO/Nano (~32 KB Flash, heavily simplified)
```

**Arguments:**

| Argument | Default | Description |
|---|---|---|
| `--input` | `timezones_dst.geojson` | Input GeoJSON from Step 1 |
| `--out-data` | `timezone_data.h` | Output: PROGMEM polygon data + zone metadata |
| `--out-lookup` | `timezone_lookup.h` | Output: lookup algorithm (header-only, no data) |
| `--max-pts` | `120` | Maximum vertices per polygon ring. Trades accuracy for Flash size |

**Recommended `--max-pts` by platform:**

| Platform | Flash | `--max-pts` | Approx. data size |
|---|---|---|---|
| ESP32 | 4 MB | 200 | ~280 KB |
| ESP8266 | 1 MB | 120 | ~150 KB |
| Arduino Mega | 256 KB | 80 | ~90 KB |
| Arduino UNO / Nano | 32 KB | 35 | ~28 KB |
---

### Full Pipeline Example

```bash
# Download, simplify, enrich DST data
python convert.py --tolerance 0.05 --release 2025b --year 2026

# Convert to Arduino headers for ESP32
python compress.py --input timezones_dst.geojson --max-pts 200

# Copy generated headers into your Arduino project folder
cp timezone_data.h timezone_lookup.h /path/to/your/sketch/
```
