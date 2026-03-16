# Module 06 — Grafana Dashboards

## Overview

Three dashboards cover all monitoring needs. All dashboards use InfluxDB 2.x as the data source with Flux query language.

---

## Dashboard 1: Per-Device Real-Time

**Title:** `HIVIS — [Device Name]`  
**Purpose:** Live monitoring for a single device  
**Refresh:** 5 seconds  
**Default time range:** Last 1 hour

### Variables

| Variable | Type | Query | Purpose |
|----------|------|-------|---------|
| `$device_id` | Query | `import "influxdata/influxdb/schema" schema.tagValues(bucket: "hivis", tag: "device_id")` | Device selector |

### Panels

**Row 1: Status Bar**

| Panel | Type | Metric | Notes |
|-------|------|--------|-------|
| IAQ | Stat | `iaq` | Color thresholds: green <100, yellow 100–200, red >200 |
| CO2 | Stat | `co2` | Unit: ppm |
| Noise | Stat | `db` | Unit: dB |
| Battery | Gauge | `bat_pct` | 0–100%, color: red <20, yellow <50, green ≥50 |
| WiFi | Stat | `rssi` | Unit: dBm |
| Accuracy | Stat | `accuracy` | 0–3, display as text: 0=Calibrating, 3=Optimal |

**Row 2: Air Quality Trend**

| Panel | Type | Metrics | Notes |
|-------|------|---------|-------|
| IAQ Over Time | Time series | `iaq` | Threshold lines at `iaq_warn` (100) and `iaq_alert` (200) |
| CO2 Over Time | Time series | `co2` | Unit: ppm |
| BVOC Over Time | Time series | `bvoc` | Unit: ppm |

**Row 3: Environment**

| Panel | Type | Metrics | Notes |
|-------|------|---------|-------|
| Temperature | Time series | `temp` | Unit: °C |
| Humidity | Time series | `hum` | Unit: % |
| Noise Level | Time series | `db` | Unit: dB SPL |

**Row 4: System**

| Panel | Type | Metrics | Notes |
|-------|------|---------|-------|
| Battery % | Time series | `bat_pct` | Unit: % |
| Battery Voltage | Time series | `bat_mv` | Unit: mV, transform ÷1000 for V display |
| WiFi RSSI | Time series | `rssi` | Unit: dBm |

### Example Flux Query (IAQ)

```flux
from(bucket: "hivis")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r.device_id == "${device_id}")
  |> filter(fn: (r) => r._field == "iaq")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "mean")
```

---

## Dashboard 2: Fleet Overview

**Title:** `HIVIS — Fleet Overview`  
**Purpose:** Status of all devices at a glance  
**Refresh:** 30 seconds  
**Default time range:** Last 15 minutes

### Panels

**Row 1: Device Status Table**

A single `Table` panel showing all devices with their latest values:

| Column | Source | Notes |
|--------|--------|-------|
| Device Name | tag `device_name` | |
| Group | tag `group` | |
| Last Seen | `_time` | Time since last data point |
| IAQ | field `iaq` | Color coded |
| CO2 | field `co2` | ppm |
| Noise | field `db` | dB |
| Battery | field `bat_pct` | % |
| RSSI | field `rssi` | dBm |
| Accuracy | field `accuracy` | 0–3 |
| Online | derived | Green if last seen < 2min, red otherwise |

Flux query for latest values per device:
```flux
from(bucket: "hivis")
  |> range(start: -15m)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "iaq" or
                       r._field == "co2" or
                       r._field == "db" or
                       r._field == "bat_pct" or
                       r._field == "rssi" or
                       r._field == "accuracy")
  |> last()
  |> pivot(rowKey:["_time", "device_id", "device_name", "group"],
           columnKey: ["_field"],
           valueColumn: "_value")
```

**Row 2: IAQ Comparison**

A multi-device time series showing IAQ for all devices on the same chart. Different color per device.

```flux
from(bucket: "hivis")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r._field == "iaq")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
```

**Row 3: Battery Overview**

Bar gauge showing current battery % for all devices side by side.

---

## Dashboard 3: Historical Trends

**Title:** `HIVIS — Historical Data`  
**Purpose:** Long-term trend analysis  
**Refresh:** 5 minutes (or manual)  
**Default time range:** Last 7 days

### Variables

| Variable | Type | Notes |
|----------|------|-------|
| `$device_id` | Query | Multi-select, default = All |
| `$interval` | Interval | Auto-calculated based on time range |

### Panels

**Row 1: IAQ History**

Time series — all selected devices, `aggregateWindow` by `$interval`, `fn: mean`.  
Annotation layer: backfill data points shown with a different marker color.

**Row 2: CO2 History**

Time series — all selected devices.

**Row 3: Temperature & Humidity**

Two panels side by side. Temperature °C and Humidity %.

**Row 4: Noise History**

Time series — dB SPL over selected period.

**Row 5: Battery Drain**

Time series — battery % per device. Useful for identifying devices needing charge.

### Backfill Annotation

To visually distinguish backfill data:
```flux
from(bucket: "hivis")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> filter(fn: (r) => r.backfill == "true")
  |> filter(fn: (r) => r.device_id == "${device_id}")
  |> filter(fn: (r) => r._field == "iaq")
```
Use as annotation query. Backfill points appear as markers on the timeline.

---

## Grafana Provisioning

Dashboards can be provisioned as JSON files so they are automatically loaded when Grafana starts.

File structure:
```
./grafana/provisioning/
  datasources/
    influxdb.yml         ← See Module 05
  dashboards/
    hivis.yml            ← Dashboard provider config
    per_device.json      ← Dashboard 1
    fleet_overview.json  ← Dashboard 2
    historical.json      ← Dashboard 3
```

Provider config (`hivis.yml`):
```yaml
apiVersion: 1

providers:
  - name: HIVIS Dashboards
    type: file
    options:
      path: /etc/grafana/provisioning/dashboards
    updateIntervalSeconds: 30
    allowUiUpdates: true
```

---

## Alert Rules (Optional, Future)

Grafana supports alerting. Not in scope for 2.0 but the data model supports it.  
When implemented, alerts can notify via email or webhook when:
- IAQ exceeds `iaq_alert` for more than 5 consecutive minutes
- Any device goes offline (no data for > 5 minutes)
- Battery below 15%
