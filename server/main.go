package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"sync"

	"github.com/prometheus/client_golang/prometheus"
)

type SolarNodeReading struct {
	Temperature    float64 `json:"temperature"`
	Pressure       float64 `json:"pressure"`
	Humidity       float64 `json:"humidity"`
	BatteryVoltage float64 `json:"battery_voltage"`
}

var (
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
	storageMutex sync.RWMutex
)

func main() {
	// cli flags
	serverPort := flag.Int("server.port", 11267, "Port on which the SolarNode server runs")
	flag.Parse()

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
	storageMutex.Lock()

	temperature.Set(newData.Temperature)
	pressure.Set(newData.Pressure)
	humidity.Set(newData.Humidity)
	batteryVoltage.Set(newData.BatteryVoltage)

	storageMutex.Unlock()

	w.WriteHeader(http.StatusNoContent)
}

func metricsHandler(w http.ResponseWriter, r *http.Request) {
}
