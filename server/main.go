package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	_ "modernc.org/sqlite"
)

// FrequencyInfo describes what each scanned frequency represents
type FrequencyInfo struct {
	MHz      string
	Label    string
	Category string
	Devices  string
	Color    string
}

// Frequency map matching the ESP32 SCAN_FREQUENCIES array
var frequencies = []FrequencyInfo{
	{"903.9", "LoRaWAN Ch0", "lorawan", "IoT sensors, industrial monitors", "#4CAF50"},
	{"906.3", "LoRaWAN Uplink", "lorawan", "Smart agriculture, asset trackers", "#8BC34A"},
	{"909.1", "LoRaWAN Mid", "lorawan", "Environmental sensors, weather stations", "#CDDC39"},
	{"911.9", "Meshtastic", "meshtastic", "Off-grid mesh communicators, hikers", "#FF9800"},
	{"914.9", "LoRaWAN", "lorawan", "Utility meters, parking sensors", "#4CAF50"},
	{"917.5", "Amazon Sidewalk", "sidewalk", "Ring, Echo, Tile, smart locks", "#00BCD4"},
	{"920.1", "LoRaWAN", "lorawan", "Smart city infrastructure", "#8BC34A"},
	{"922.9", "LoRaWAN Downlink", "lorawan", "Gateway responses, ACKs", "#009688"},
}

// Stats represents a single upload from a LoRa detector
type Stats struct {
	DeviceID         string    `json:"device_id"`
	Uptime           int       `json:"uptime_seconds"`
	TotalDetections  int       `json:"total_detections"`
	DetectionsPerMin int       `json:"detections_per_min"`
	CurrentActivity  int       `json:"current_activity_pct"`
	PeakActivity     int       `json:"peak_activity_pct"`
	FreqDetections   []int     `json:"freq_detections"`
	Timestamp        time.Time `json:"timestamp"`
	UploaderIP       string    `json:"uploader_ip"`
}

// PeriodSummary holds aggregated stats for a time period
type PeriodSummary struct {
	Label           string
	Days            int
	TotalUploads    int
	TotalDetections int
	TotalScanTime   int // seconds
	AvgDetPerMin    float64
	AvgActivity     float64
	PeakActivity    int
	FreqTotals      []int // Per-frequency totals
}

// Store keeps track of all uploads (in-memory cache + SQLite)
type Store struct {
	mu     sync.RWMutex
	latest map[string]Stats // Latest per device (in-memory)
	db     *sql.DB
}

var store *Store

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}

	// Initialize database
	dbPath := os.Getenv("DB_PATH")
	if dbPath == "" {
		dbPath = "/data/lora.db"
	}

	// Ensure data directory exists
	if err := os.MkdirAll("/data", 0755); err != nil {
		// Fall back to current directory if /data isn't available
		dbPath = "./lora.db"
	}

	db, err := initDB(dbPath)
	if err != nil {
		log.Fatalf("Failed to initialize database: %v", err)
	}

	store = &Store{
		latest: make(map[string]Stats),
		db:     db,
	}

	// Load latest stats from DB
	store.loadLatest()

	http.HandleFunc("/", handleHome)
	http.HandleFunc("/upload", handleUpload)
	http.HandleFunc("/stats", handleStats)
	http.HandleFunc("/api/stats", handleAPIStats)
	http.HandleFunc("/api/history", handleAPIHistory)

	log.Printf("LoRa Detector Server starting on port %s (DB: %s)", port, dbPath)
	log.Fatal(http.ListenAndServe(":"+port, nil))
}

func initDB(path string) (*sql.DB, error) {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}

	// Create tables
	schema := `
	CREATE TABLE IF NOT EXISTS uploads (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		device_id TEXT NOT NULL,
		timestamp DATETIME NOT NULL,
		uptime_seconds INTEGER,
		total_detections INTEGER,
		detections_per_min INTEGER,
		current_activity_pct INTEGER,
		peak_activity_pct INTEGER,
		freq_0 INTEGER DEFAULT 0,
		freq_1 INTEGER DEFAULT 0,
		freq_2 INTEGER DEFAULT 0,
		freq_3 INTEGER DEFAULT 0,
		freq_4 INTEGER DEFAULT 0,
		freq_5 INTEGER DEFAULT 0,
		freq_6 INTEGER DEFAULT 0,
		freq_7 INTEGER DEFAULT 0,
		uploader_ip TEXT
	);

	CREATE INDEX IF NOT EXISTS idx_uploads_timestamp ON uploads(timestamp);
	CREATE INDEX IF NOT EXISTS idx_uploads_device ON uploads(device_id);
	`

	_, err = db.Exec(schema)
	if err != nil {
		return nil, err
	}

	// Clean up old data (older than 1 year)
	_, err = db.Exec(`DELETE FROM uploads WHERE timestamp < datetime('now', '-365 days')`)
	if err != nil {
		log.Printf("Warning: failed to clean old data: %v", err)
	}

	return db, nil
}

func (s *Store) loadLatest() {
	rows, err := s.db.Query(`
		SELECT device_id, timestamp, uptime_seconds, total_detections,
			   detections_per_min, current_activity_pct, peak_activity_pct,
			   freq_0, freq_1, freq_2, freq_3, freq_4, freq_5, freq_6, freq_7, uploader_ip
		FROM uploads
		WHERE id IN (SELECT MAX(id) FROM uploads GROUP BY device_id)
	`)
	if err != nil {
		log.Printf("Error loading latest stats: %v", err)
		return
	}
	defer rows.Close()

	s.mu.Lock()
	defer s.mu.Unlock()

	for rows.Next() {
		var stats Stats
		var ts string
		var f0, f1, f2, f3, f4, f5, f6, f7 int
		err := rows.Scan(&stats.DeviceID, &ts, &stats.Uptime, &stats.TotalDetections,
			&stats.DetectionsPerMin, &stats.CurrentActivity, &stats.PeakActivity,
			&f0, &f1, &f2, &f3, &f4, &f5, &f6, &f7, &stats.UploaderIP)
		if err != nil {
			log.Printf("Error scanning row: %v", err)
			continue
		}
		stats.FreqDetections = []int{f0, f1, f2, f3, f4, f5, f6, f7}
		stats.Timestamp, _ = time.Parse("2006-01-02 15:04:05", ts)
		s.latest[stats.DeviceID] = stats
	}
	log.Printf("Loaded %d devices from database", len(s.latest))
}

func (s *Store) saveUpload(stats Stats) error {
	freqs := make([]int, 8)
	for i := 0; i < 8 && i < len(stats.FreqDetections); i++ {
		freqs[i] = stats.FreqDetections[i]
	}

	_, err := s.db.Exec(`
		INSERT INTO uploads (device_id, timestamp, uptime_seconds, total_detections,
			detections_per_min, current_activity_pct, peak_activity_pct,
			freq_0, freq_1, freq_2, freq_3, freq_4, freq_5, freq_6, freq_7, uploader_ip)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
	`, stats.DeviceID, stats.Timestamp.Format("2006-01-02 15:04:05"),
		stats.Uptime, stats.TotalDetections, stats.DetectionsPerMin,
		stats.CurrentActivity, stats.PeakActivity,
		freqs[0], freqs[1], freqs[2], freqs[3], freqs[4], freqs[5], freqs[6], freqs[7],
		stats.UploaderIP)

	return err
}

func (s *Store) getSummary(days int) PeriodSummary {
	summary := PeriodSummary{
		Days:       days,
		FreqTotals: make([]int, 8),
	}

	row := s.db.QueryRow(`
		SELECT
			COUNT(*) as uploads,
			COALESCE(SUM(total_detections), 0) as total_det,
			COALESCE(SUM(uptime_seconds), 0) as total_time,
			COALESCE(AVG(detections_per_min), 0) as avg_dpm,
			COALESCE(AVG(current_activity_pct), 0) as avg_act,
			COALESCE(MAX(peak_activity_pct), 0) as peak,
			COALESCE(SUM(freq_0), 0), COALESCE(SUM(freq_1), 0),
			COALESCE(SUM(freq_2), 0), COALESCE(SUM(freq_3), 0),
			COALESCE(SUM(freq_4), 0), COALESCE(SUM(freq_5), 0),
			COALESCE(SUM(freq_6), 0), COALESCE(SUM(freq_7), 0)
		FROM uploads
		WHERE timestamp > datetime('now', ? || ' days')
	`, fmt.Sprintf("-%d", days))

	err := row.Scan(&summary.TotalUploads, &summary.TotalDetections, &summary.TotalScanTime,
		&summary.AvgDetPerMin, &summary.AvgActivity, &summary.PeakActivity,
		&summary.FreqTotals[0], &summary.FreqTotals[1], &summary.FreqTotals[2], &summary.FreqTotals[3],
		&summary.FreqTotals[4], &summary.FreqTotals[5], &summary.FreqTotals[6], &summary.FreqTotals[7])
	if err != nil {
		log.Printf("Error getting summary for %d days: %v", days, err)
	}

	return summary
}

func (s *Store) getTotalUploads() int {
	var count int
	s.db.QueryRow(`SELECT COUNT(*) FROM uploads`).Scan(&count)
	return count
}

func handleHome(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	store.mu.RLock()
	latest := make(map[string]Stats)
	for k, v := range store.latest {
		latest[k] = v
	}
	store.mu.RUnlock()

	// Get summaries
	summaries := []PeriodSummary{
		store.getSummary(7),
		store.getSummary(30),
		store.getSummary(90),
		store.getSummary(365),
	}
	summaries[0].Label = "7 Days"
	summaries[1].Label = "30 Days"
	summaries[2].Label = "90 Days"
	summaries[3].Label = "1 Year"

	totalUploads := store.getTotalUploads()

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprintf(w, `<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>LoRa Detector Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="30">
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', system-ui, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%%, #16213e 100%%);
            color: #e0e0e0;
            padding: 20px;
            margin: 0;
            min-height: 100vh;
        }
        .container { max-width: 1000px; margin: 0 auto; }
        h1 {
            color: #00d4ff;
            text-align: center;
            font-size: 2em;
            margin-bottom: 5px;
            text-shadow: 0 0 20px rgba(0,212,255,0.5);
        }
        .subtitle {
            text-align: center;
            color: #888;
            margin-bottom: 30px;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-bottom: 30px;
        }
        .stat-box {
            background: rgba(255,255,255,0.05);
            border-radius: 12px;
            padding: 20px;
            text-align: center;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .stat-box .value {
            font-size: 2.5em;
            font-weight: bold;
            color: #00d4ff;
        }
        .stat-box .label { color: #888; font-size: 0.9em; }
        .stat-box.hot .value { color: #ff4444; animation: pulse 1s infinite; }
        @keyframes pulse { 50%% { opacity: 0.7; } }

        .card {
            background: rgba(255,255,255,0.05);
            border-radius: 16px;
            padding: 25px;
            margin-bottom: 25px;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .card h2 {
            color: #fff;
            margin: 0 0 20px 0;
            font-size: 1.3em;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .card h2 .icon { font-size: 1.5em; }

        /* Frequency breakdown */
        .freq-table { width: 100%%; }
        .freq-row {
            display: grid;
            grid-template-columns: 80px 140px 1fr 80px;
            gap: 15px;
            padding: 12px 0;
            border-bottom: 1px solid rgba(255,255,255,0.05);
            align-items: center;
        }
        .freq-row:last-child { border-bottom: none; }
        .freq-mhz {
            font-family: 'Courier New', monospace;
            font-weight: bold;
            color: #fff;
        }
        .freq-label { color: #aaa; font-size: 0.9em; }
        .freq-bar-container {
            background: rgba(255,255,255,0.1);
            border-radius: 4px;
            height: 24px;
            overflow: hidden;
        }
        .freq-bar {
            height: 100%%;
            border-radius: 4px;
            display: flex;
            align-items: center;
            padding-left: 8px;
            font-size: 0.8em;
            font-weight: bold;
            color: #000;
            transition: width 0.5s ease;
        }
        .freq-count {
            font-family: 'Courier New', monospace;
            text-align: right;
            color: #fff;
        }

        /* Category summary */
        .category-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
        }
        .category-card {
            background: rgba(0,0,0,0.3);
            border-radius: 12px;
            padding: 20px;
            border-left: 4px solid;
        }
        .category-card.sidewalk { border-left-color: #00BCD4; }
        .category-card.meshtastic { border-left-color: #FF9800; }
        .category-card.lorawan { border-left-color: #4CAF50; }
        .category-card h3 {
            margin: 0 0 10px 0;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .category-card .count {
            font-size: 2em;
            font-weight: bold;
            margin-bottom: 10px;
        }
        .category-card.sidewalk .count { color: #00BCD4; }
        .category-card.meshtastic .count { color: #FF9800; }
        .category-card.lorawan .count { color: #4CAF50; }
        .category-card .devices {
            font-size: 0.85em;
            color: #999;
            line-height: 1.6;
        }

        /* Device info */
        .device-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 10px;
        }
        .device-id {
            background: rgba(0,212,255,0.2);
            padding: 5px 15px;
            border-radius: 20px;
            color: #00d4ff;
            font-family: monospace;
        }
        .timestamp { color: #666; font-size: 0.85em; }

        .no-data {
            text-align: center;
            padding: 60px 20px;
            color: #666;
        }
        .no-data .icon { font-size: 4em; margin-bottom: 20px; }
        .no-data p { margin: 10px 0; }

        .legend {
            display: flex;
            gap: 20px;
            flex-wrap: wrap;
            justify-content: center;
            margin-top: 20px;
            padding-top: 20px;
            border-top: 1px solid rgba(255,255,255,0.1);
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 6px;
            font-size: 0.85em;
            color: #888;
        }
        .legend-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%%;
        }

        /* Historical summaries */
        .summary-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 15px;
        }
        .summary-card {
            background: rgba(0,0,0,0.3);
            border-radius: 12px;
            padding: 20px;
            border-top: 3px solid #00d4ff;
        }
        .summary-card h3 {
            margin: 0 0 15px 0;
            color: #00d4ff;
            font-size: 1.1em;
        }
        .summary-stat {
            display: flex;
            justify-content: space-between;
            padding: 6px 0;
            border-bottom: 1px solid rgba(255,255,255,0.05);
        }
        .summary-stat:last-child { border-bottom: none; }
        .summary-stat .label { color: #888; }
        .summary-stat .value { color: #fff; font-weight: bold; }
        .summary-card .mini-freq {
            display: flex;
            gap: 4px;
            margin-top: 10px;
        }
        .mini-freq .bar {
            flex: 1;
            height: 20px;
            border-radius: 2px;
            position: relative;
        }
        .mini-freq .bar span {
            position: absolute;
            bottom: -16px;
            left: 50%%;
            transform: translateX(-50%%);
            font-size: 0.65em;
            color: #666;
        }

        footer {
            text-align: center;
            color: #444;
            margin-top: 40px;
            padding-top: 20px;
            border-top: 1px solid rgba(255,255,255,0.05);
        }
        .db-badge {
            display: inline-block;
            background: rgba(0,212,255,0.1);
            padding: 3px 10px;
            border-radius: 10px;
            font-size: 0.8em;
            color: #00d4ff;
            margin-left: 10px;
        }
    </style>
</head>
<body>
<div class="container">
    <h1>📡 LoRa Detector Dashboard</h1>
    <p class="subtitle">900 MHz ISM Band Activity Monitor <span class="db-badge">%d uploads stored</span></p>
`, totalUploads)

	if len(latest) == 0 {
		fmt.Fprintf(w, `
    <div class="no-data">
        <div class="icon">📻</div>
        <p><strong>No data received yet</strong></p>
        <p>Double-click the PRG button on your LoRa detector to upload!</p>
        <p style="margin-top: 30px; font-size: 0.9em;">
            The detector scans 8 frequencies across 903-923 MHz<br>
            detecting Amazon Sidewalk, LoRaWAN, and Meshtastic signals.
        </p>
    </div>
`)
	}

	for deviceID, stats := range latest {
		// Calculate category totals
		sidewalkCount := 0
		meshtasticCount := 0
		lorawanCount := 0

		if len(stats.FreqDetections) >= 8 {
			sidewalkCount = stats.FreqDetections[5]
			meshtasticCount = stats.FreqDetections[3]
			lorawanCount = stats.FreqDetections[0] + stats.FreqDetections[1] +
				stats.FreqDetections[2] + stats.FreqDetections[4] +
				stats.FreqDetections[6] + stats.FreqDetections[7]
		}

		// Find max for bar scaling
		maxCount := 1
		for _, c := range stats.FreqDetections {
			if c > maxCount {
				maxCount = c
			}
		}

		hotClass := ""
		if stats.CurrentActivity >= 10 {
			hotClass = "hot"
		}

		// Overview stats
		fmt.Fprintf(w, `
    <div class="card">
        <h2><span class="icon">📊</span> Latest Session</h2>
        <div class="stats-grid">
            <div class="stat-box">
                <div class="value">%d</div>
                <div class="label">Total Detections</div>
            </div>
            <div class="stat-box">
                <div class="value">%d</div>
                <div class="label">Per Minute</div>
            </div>
            <div class="stat-box %s">
                <div class="value">%d%%</div>
                <div class="label">Activity</div>
            </div>
            <div class="stat-box">
                <div class="value">%d%%</div>
                <div class="label">Peak</div>
            </div>
            <div class="stat-box">
                <div class="value">%02d:%02d</div>
                <div class="label">Scan Time</div>
            </div>
        </div>
        <div class="device-header" style="margin-top: 15px;">
            <span class="device-id">%s</span>
            <span class="timestamp">%s</span>
        </div>
    </div>
`, stats.TotalDetections, stats.DetectionsPerMin,
			hotClass, stats.CurrentActivity, stats.PeakActivity,
			stats.Uptime/3600, (stats.Uptime%3600)/60,
			deviceID, stats.Timestamp.Format("Jan 2, 2006 at 3:04 PM MST"))

		// Category breakdown
		fmt.Fprintf(w, `
    <div class="card">
        <h2><span class="icon">🔍</span> What You Detected</h2>
        <div class="category-grid">
            <div class="category-card sidewalk">
                <h3>🏠 Amazon Sidewalk</h3>
                <div class="count">%d</div>
                <div class="devices">
                    Ring doorbells & cameras<br>
                    Echo (4th gen+) speakers<br>
                    Tile trackers<br>
                    Level smart locks
                </div>
            </div>
            <div class="category-card meshtastic">
                <h3>🥾 Meshtastic</h3>
                <div class="count">%d</div>
                <div class="devices">
                    Off-grid mesh communicators<br>
                    Hiker/outdoor devices<br>
                    Emergency comms<br>
                    DIY LoRa nodes
                </div>
            </div>
            <div class="category-card lorawan">
                <h3>🏭 LoRaWAN / IoT</h3>
                <div class="count">%d</div>
                <div class="devices">
                    Smart utility meters<br>
                    Parking sensors<br>
                    Agricultural monitors<br>
                    Industrial sensors
                </div>
            </div>
        </div>
    </div>
`, sidewalkCount, meshtasticCount, lorawanCount)

		// Frequency breakdown table
		fmt.Fprintf(w, `
    <div class="card">
        <h2><span class="icon">📶</span> Frequency Breakdown</h2>
        <div class="freq-table">
`)
		for i, freq := range frequencies {
			count := 0
			if i < len(stats.FreqDetections) {
				count = stats.FreqDetections[i]
			}
			barWidth := 0
			if maxCount > 0 {
				barWidth = (count * 100) / maxCount
			}
			if barWidth < 2 && count > 0 {
				barWidth = 2
			}

			fmt.Fprintf(w, `
            <div class="freq-row">
                <div class="freq-mhz">%s</div>
                <div class="freq-label">%s</div>
                <div class="freq-bar-container">
                    <div class="freq-bar" style="width: %d%%; background: %s;">%s</div>
                </div>
                <div class="freq-count">%d</div>
            </div>
`, freq.MHz, freq.Label, barWidth, freq.Color, freq.Devices, count)
		}

		fmt.Fprintf(w, `
        </div>
        <div class="legend">
            <div class="legend-item"><div class="legend-dot" style="background: #00BCD4;"></div> Amazon Sidewalk</div>
            <div class="legend-item"><div class="legend-dot" style="background: #FF9800;"></div> Meshtastic</div>
            <div class="legend-item"><div class="legend-dot" style="background: #4CAF50;"></div> LoRaWAN</div>
        </div>
    </div>
`)
	}

	// Historical Summaries
	fmt.Fprintf(w, `
    <div class="card">
        <h2><span class="icon">📈</span> Historical Summary</h2>
        <div class="summary-grid">
`)

	for _, s := range summaries {
		scanHours := s.TotalScanTime / 3600
		scanMins := (s.TotalScanTime % 3600) / 60

		// Calculate max for mini bars
		maxFreq := 1
		for _, f := range s.FreqTotals {
			if f > maxFreq {
				maxFreq = f
			}
		}

		fmt.Fprintf(w, `
            <div class="summary-card">
                <h3>%s</h3>
                <div class="summary-stat">
                    <span class="label">Uploads</span>
                    <span class="value">%d</span>
                </div>
                <div class="summary-stat">
                    <span class="label">Detections</span>
                    <span class="value">%d</span>
                </div>
                <div class="summary-stat">
                    <span class="label">Scan Time</span>
                    <span class="value">%dh %dm</span>
                </div>
                <div class="summary-stat">
                    <span class="label">Avg Det/min</span>
                    <span class="value">%.1f</span>
                </div>
                <div class="summary-stat">
                    <span class="label">Peak Activity</span>
                    <span class="value">%d%%</span>
                </div>
                <div class="mini-freq">
`, s.Label, s.TotalUploads, s.TotalDetections, scanHours, scanMins,
			s.AvgDetPerMin, s.PeakActivity)

		// Mini frequency bars
		for i, freq := range frequencies {
			height := 0
			if maxFreq > 0 && i < len(s.FreqTotals) {
				height = (s.FreqTotals[i] * 100) / maxFreq
			}
			if height < 5 && s.FreqTotals[i] > 0 {
				height = 5
			}
			fmt.Fprintf(w, `                    <div class="bar" style="background: linear-gradient(to top, %s %d%%, rgba(255,255,255,0.1) %d%%);"><span>%s</span></div>
`, freq.Color, height, height, freq.MHz[:3])
		}

		fmt.Fprintf(w, `                </div>
            </div>
`)
	}

	fmt.Fprintf(w, `
        </div>
    </div>
`)

	fmt.Fprintf(w, `
    <footer>
        Auto-refreshes every 30 seconds · Data retained for 1 year · Built with Claude Code
    </footer>
</div>
</body>
</html>`)
}

func handleUpload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST required", http.StatusMethodNotAllowed)
		return
	}

	var stats Stats
	if err := json.NewDecoder(r.Body).Decode(&stats); err != nil {
		log.Printf("Error decoding JSON: %v", err)
		http.Error(w, "Invalid JSON", http.StatusBadRequest)
		return
	}

	stats.Timestamp = time.Now()
	stats.UploaderIP = r.RemoteAddr

	if stats.DeviceID == "" {
		stats.DeviceID = "unknown"
	}

	// Save to database
	if err := store.saveUpload(stats); err != nil {
		log.Printf("Error saving to database: %v", err)
	}

	// Update in-memory cache
	store.mu.Lock()
	store.latest[stats.DeviceID] = stats
	store.mu.Unlock()

	log.Printf("Upload from %s: %d total detections, %d/min, %d%% activity",
		stats.DeviceID, stats.TotalDetections, stats.DetectionsPerMin, stats.CurrentActivity)
	if len(stats.FreqDetections) >= 8 {
		log.Printf("  Frequencies: 903.9=%d, 906.3=%d, 909.1=%d, 911.9=%d, 914.9=%d, 917.5=%d, 920.1=%d, 922.9=%d",
			stats.FreqDetections[0], stats.FreqDetections[1], stats.FreqDetections[2], stats.FreqDetections[3],
			stats.FreqDetections[4], stats.FreqDetections[5], stats.FreqDetections[6], stats.FreqDetections[7])
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status":  "ok",
		"message": fmt.Sprintf("Received %d detections", stats.TotalDetections),
	})
}

func handleStats(w http.ResponseWriter, r *http.Request) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	w.Header().Set("Content-Type", "text/plain")
	fmt.Fprintf(w, "LoRa Detector Stats\n")
	fmt.Fprintf(w, "==================\n\n")
	fmt.Fprintf(w, "Total uploads in database: %d\n\n", store.getTotalUploads())

	for deviceID, stats := range store.latest {
		fmt.Fprintf(w, "Device: %s\n", deviceID)
		fmt.Fprintf(w, "  Uptime: %02d:%02d:%02d\n", stats.Uptime/3600, (stats.Uptime%3600)/60, stats.Uptime%60)
		fmt.Fprintf(w, "  Total Detections: %d\n", stats.TotalDetections)
		fmt.Fprintf(w, "  Det/min: %d\n", stats.DetectionsPerMin)
		fmt.Fprintf(w, "  Activity: %d%% (peak: %d%%)\n", stats.CurrentActivity, stats.PeakActivity)

		if len(stats.FreqDetections) >= 8 {
			fmt.Fprintf(w, "\n  Frequency Breakdown:\n")
			for i, freq := range frequencies {
				fmt.Fprintf(w, "    %s MHz %-18s: %d\n", freq.MHz, "("+freq.Label+")", stats.FreqDetections[i])
			}
		}

		fmt.Fprintf(w, "\n  Last upload: %s\n\n", stats.Timestamp.Format(time.RFC3339))
	}
}

func handleAPIStats(w http.ResponseWriter, r *http.Request) {
	store.mu.RLock()
	defer store.mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"total_uploads": store.getTotalUploads(),
		"devices":       store.latest,
		"frequencies":   frequencies,
	})
}

func handleAPIHistory(w http.ResponseWriter, r *http.Request) {
	summaries := map[string]PeriodSummary{
		"7days":   store.getSummary(7),
		"30days":  store.getSummary(30),
		"90days":  store.getSummary(90),
		"365days": store.getSummary(365),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(summaries)
}
