package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

type SolarNodeReading struct {
	Temperature    float64 `json:"temperature"`
	Pressure       float64 `json:"pressure"`
	Humidity       float64 `json:"humidity"`
	BatteryVoltage float64 `json:"battery_voltage"`
}

var (
	serverPort *int
	maxAge     *time.Duration
	lastUpdate time.Time

	temperature = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "solarnode_temperature_celsius",
		Help: "Sensor temperature measured (BME280)",
	})
	pressure = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "solarnode_pressure_pascals",
		Help: "Atmospheric temperature measured (BME280)",
	})
	humidity = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "solarnode_humidity_relative",
		Help: "Relative humidity measured (BME280)",
	})
	batteryVoltage = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "solarnode_battery_voltage_volts",
		Help: "Battery supply voltage (voltage divider ADC reading)",
	})
	dataAgeSeconds = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "solarnode_data_age_seconds",
		Help: "Time passed since the last sensor data update in seconds",
	})
)

func healthHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)

	err := json.NewEncoder(w).Encode(map[string]string{"status": "alive"})
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		log.Printf("Encoder error: %v\n", err)
		return
	}
}

func dataHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.NotFound(w, r)
		return
	}

	var newData SolarNodeReading

	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()

	err := decoder.Decode(&newData)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	log.Printf("/data\tNew data recieved: %+v\n", newData)

	// update data
	temperature.Set(newData.Temperature)
	pressure.Set(newData.Pressure)
	humidity.Set(newData.Humidity)
	batteryVoltage.Set(newData.BatteryVoltage)
	dataAgeSeconds.Set(0)

	lastUpdate = time.Now()

	w.WriteHeader(http.StatusNoContent)
}

func metricsHandler(w http.ResponseWriter, r *http.Request) {
	if time.Since(lastUpdate) > *maxAge {
		http.Error(w, "Stale or unavailable sensor data", http.StatusServiceUnavailable)
		return
	}

	dataAgeSeconds.Set(time.Since(lastUpdate).Seconds())

	promhttp.Handler().ServeHTTP(w, r)
}

func main() {
	// cli flags
	serverPort = flag.Int("port", 11267,
		"Port on which the SolarNode server runs")
	maxAge = flag.Duration("max_age", 5*time.Minute,
		"Maximum age of sensor data in minutes")
	flag.Parse()

	// prometheus setup
	prometheus.MustRegister(temperature, pressure, humidity, batteryVoltage, dataAgeSeconds)

	// route setup
	mux := http.NewServeMux()

	mux.HandleFunc("/health", healthHandler)
	mux.HandleFunc("/{$}", healthHandler)

	mux.HandleFunc("/data", dataHandler)

	mux.HandleFunc("/metrics", metricsHandler)

	// server start
	serverPortString := fmt.Sprintf(":%d", *serverPort)
	log.Printf("Starting server on %s\n", serverPortString)

	err := http.ListenAndServe(serverPortString, mux)
	if err != nil {
		log.Fatalf("Couldn't start server: %v\n", err)
	}
}
