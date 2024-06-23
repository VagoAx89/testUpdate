

//Bibliotecas para ejectuar el Update
#include <Update.h>
#include <FS.h>
#include <SD.h>

//Bibliotecas para manejar la conexion Ethernet
#include <Arduino.h>
#include <Ethernet.h>
#include <ESP_SSLClient.h>

//Variables para Ethernet
ESP_SSLClient ssl_client;
EthernetClient basic_client;
uint8_t Eth_MAC[] = { 0x02, 0xF0, 0x0D, 0xBE, 0xEF, 0x01 };

//Declaracion de booleana para eliminar headers
bool headersEnd = false;

//Declaracion del led por defecto
int led = 2;

//Declaracion de version
int version = 1;

void setup() {

  Serial.begin(115200);
  Serial.println("Ejemplo de actualizacion desde servidor github");


  pinMode(led, OUTPUT);  //Led como salida

  //Se inicia memoria SD y se reinicia si no se ha iniciado
  SPI.begin();  //Inicializacion de protocolo SPI para ethernet y tarjeta SD
  Serial.println("Iniciando sd");
  if (!SD.begin(21)) {  //En mi caso estoy unsando el Pin 21 del esp32 como pin seleccionador del puerto de memoria SD
    Serial.println("Falló la inicializacion de tarjet SD, reiniciando...");
    delay(1000);
    ESP.restart();  //Reiniciar el Esp
  }
  delay(100);  //Tuve algunos problemas sin este delay, el puerto Ethernet no se iniciaba
  //Se inicia Ethernet
  Ethernet.init(5);  //El pin seleccionador es el 5 (ese es el pin por defecto para el esp32)

  Serial.println("Iniciando conexion Ethernet");
  Ethernet.begin(Eth_MAC);  //Se inicia Ethernet con la direccion Mac antes declarada

  unsigned long to = millis();
  while (Ethernet.linkStatus() == LinkOFF || millis() - to < 2000) {
    delay(100);
  }

  if (Ethernet.linkStatus() == LinkON) {
    Serial.print("Conectado por Ethernet, IP: ");
    Serial.println(Ethernet.localIP());
  } else {
    Serial.println("No se puede conectar al puerto Ethernet");
    ESP.restart();  // Reiniciar el Esp
  }

  //Ignorar verificacion de certificado ssl del servidor
  ssl_client.setInsecure();

  // Configurar tamaño de bufer para comunicacion cliente/servidor
  ssl_client.setBufferSizes(16384 /* rx */, 1024 /* tx */);
  ssl_client.setDebugLevel(0);
  ssl_client.setClient(&basic_client);



  Serial.println("Listo para actualizar, esperando comando.");
}

//will not be reached
void loop() {

  digitalWrite(led, HIGH);
  delay(1000);
  digitalWrite(led, LOW);
  delay(1000);

  if (Serial.available() > 0) {
    uint8_t cardType;
    String input = Serial.readStringUntil('\n');
    if (input.equals("actualizar")) {
      Serial.println("Se va iniciar la actualizacion");
      delay(2000);
      cardType = SD.cardType();

      if (cardType == CARD_NONE) {
        Serial.println("No se puede acceder a tarjeta SD, reiniciando...");
        ESP.restart();  //Reiniciar si tarjeta SD no esta disponible
      } else {
        updateFromFS(SD);
      }
    }

    if (input.equals("descargar")) {
      Serial.println("Se va iniciar la descarga del firmware");
      delay(2000);
      cardType = SD.cardType();

      if (cardType == CARD_NONE) {
        Serial.println("No se puede acceder a tarjeta SD, reiniciando...");
        ESP.restart();  //Reiniciar si tarjeta SD no esta disponible
      } else {
        downloadFirmware();
      }
    }
  }
}


// perform the actual update from a given stream
void performUpdate(Stream &updateSource, size_t updateSize) {
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      Serial.println("Written : " + String(written) + " successfully");
    } else {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    if (Update.end()) {
      Serial.println("OTA done!");
      if (Update.isFinished()) {
        Serial.println("Completada la actualizacion... reiniciando");
        ESP.restart();  //Reiniciar si tarjeta SD no esta disponible
      } else {
        Serial.println("Update not finished? Something went wrong!");
      }
    } else {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
    }

  } else {
    Serial.println("Not enough space to begin OTA");
  }
}

// check given FS for valid update.bin and perform update if available
void updateFromFS(fs::FS &fs) {
  File updateBin = SD.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      Serial.println("Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      Serial.println("Try to start update");
      performUpdate(updateBin, updateSize);
    } else {
      Serial.println("Error, file is empty");
    }

    updateBin.close();

    // whe finished remove the binary from sd card to indicate end of the process
    // fs.remove("/update.bin");
  } else {
    Serial.println("Could not load update.bin from sd root");
  }
}

void downloadFirmware() {
  Serial.println("---------------------------------");
  Serial.print("Conectando al servidor...");

  File updateFile = SD.open("/update.bin", FILE_WRITE);
  if (!updateFile) {
    Serial.println("Error al abrir el archivo en la SD");
    return;
  }


  if (ssl_client.connect("raw.githubusercontent.com", 443)) {
    Serial.println(" ok");
    Serial.println("Send GET request...");

    ssl_client.println("GET /VagoAx89/testUpdate/main/update.bin HTTP/1.1");
    ssl_client.println("Host: raw.githubusercontent.com");
    ssl_client.println("");  //Indispensable mandar esta linea vacia como señal que termina la solicitud.
    ssl_client.println("Connection: close");
    Serial.println("Esperando respuesta...");

    while (!ssl_client.available()) {
      delay(100);
    }

    // Ignorar HTTP headers
    while (ssl_client.available() && !headersEnd) {
      String line = ssl_client.readStringUntil('\n');
      Serial.println(line);
      line.trim();
      if (line == "\r" || line == "\n" || line == "") {
        // Fin de los encabezados, preparados para leer el cuerpo de la respuesta
        headersEnd = true;
      }
    }

    Serial.println("");
    size_t bytesWritten = 0;

    // Lee los datos de la respuesta y escribe en el archivo
    while (ssl_client.connected() || ssl_client.available()) {
      while (ssl_client.available()) {
        uint8_t buffer[1024]; //Muy importante usar un valor alto para evitar errores... a 128 me da errores, todo depende del tamaño del archivo .bin a descargar... con 1024 
        //me sirve hasta para archivos de 1mb
        size_t bytesRead = ssl_client.read(buffer, sizeof(buffer));
        updateFile.write(buffer, bytesRead);
        bytesWritten += bytesRead;
      }
    }

    updateFile.close();
    Serial.println("Descarga completada");
    Serial.print("Bytes escritos: ");
    Serial.println(bytesWritten);

  } else {
    Serial.println(" falló");
    Serial.println("Conexión fallida");
  }
  ssl_client.println("Connection: close");
  ssl_client.stop();
  Serial.println("Conexión cerrada");
}
