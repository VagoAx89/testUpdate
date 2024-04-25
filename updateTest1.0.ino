// Incluimos la librería para ESP32
#include <Arduino.h>

// Definimos el pin al que está conectado el LED
#define LED_PIN 2

void setup() {
  // Inicializamos el pin del LED como salida
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  // Encendemos el LED
  digitalWrite(LED_PIN, HIGH);
  // Esperamos 1 segundo
  delay(200);
  // Apagamos el LED
  digitalWrite(LED_PIN, LOW);
  // Esperamos otro segundo
  delay(200);
}
