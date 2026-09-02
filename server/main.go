package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
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
		log.Printf("Encoder error: %v\n", err)
	}
}

func dataHandler(w http.ResponseWriter, r *http.Request) {
	fmt.Fprintf(w, "Processing sensor data...\n")
}

func metricsHandler(w http.ResponseWriter, r *http.Request) {
	fmt.Fprintf(w, "Prom metrics...\n")
}
