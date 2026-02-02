#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <PubSubClient.h>
#include "DHT.h"
#define DHTTYPE DHT11 // DHT 11

#define dht_dpin 4
DHT dht(dht_dpin, DHTTYPE);

#include "secrets.h"

//Conexión a Wifi
//Nombre de la red Wifi
// const char* ssid = "ARIE";
// const char* password = "31379752305";
const char ssid[] = "ARIE";
//Contraseña de la red Wifi
const char pass[] = "31379752305";

//Usuario uniandes sin @uniandes.edu.co
#define HOSTNAME "n.ibarra"

//Conexión a Mosquitto
const char MQTT_HOST[] = "iotlab.virtual.uniandes.edu.co";
const int MQTT_PORT = 8082;
//Usuario uniandes sin @uniandes.edu.co
const char MQTT_USER[] = "n.ibarra";
//Contraseña de MQTT que recibió por correo
const char MQTT_PASS[] = "202410791";
const char MQTT_SUB_TOPIC[] = HOSTNAME "/";
//Tópico al que se enviarán los datos de humedad
const char MQTT_PUB_TOPIC1[] = "humedad/pasto/" HOSTNAME;
//Tópico al que se enviarán los datos de temperatura
const char MQTT_PUB_TOPIC2[] = "temperatura/pasto/" HOSTNAME;

//topico al que se enviaralos datos de luminosidad
const char MQTT_PUB_TOPIC3[] = "luminosidad/pasto/" HOSTNAME;

//////////////////////////////////////////////////////

#if (defined(CHECK_PUB_KEY) and defined(CHECK_CA_ROOT)) or (defined(CHECK_PUB_KEY) and defined(CHECK_FINGERPRINT)) or (defined(CHECK_FINGERPRINT) and defined(CHECK_CA_ROOT)) or (defined(CHECK_PUB_KEY) and defined(CHECK_CA_ROOT) and defined(CHECK_FINGERPRINT))
  #error "cant have both CHECK_CA_ROOT and CHECK_PUB_KEY enabled"
  //net.setInsecure();
#endif

BearSSL::WiFiClientSecure net;
PubSubClient client(net);

time_t now;
unsigned long lastMillis = 0;

//Función que conecta el node a través del protocolo MQTT
//Emplea los datos de usuario y contraseña definidos en MQTT_USER y MQTT_PASS para la conexión
void mqtt_connect()
{
  //Intenta realizar la conexión indefinidamente hasta que lo logre
  while (!client.connected()) {
    Serial.print("Time: ");
    Serial.print(ctime(&now));
    Serial.print("MQTT connecting ... ");
    if (client.connect(HOSTNAME, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected.");
    } else {
      Serial.println("Problema con la conexión, revise los valores de las constantes MQTT");
      Serial.print("Código de error = ");
      Serial.println(client.state());
      if ( client.state() == MQTT_CONNECT_UNAUTHORIZED ) {
        ESP.deepSleep(0);
      }
      /* Espera 5 segundos antes de volver a intentar */
      delay(5000);
    }
  }
}

void receivedCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
}

//Configura la conexión del node MCU a Wifi y a Mosquitto
void setup()
{
  Serial.begin(115200);
  dht.begin();
  Serial.println();
  Serial.println();
  Serial.print("Attempting to connect to SSID: ");
  Serial.print(ssid);
  WiFi.hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  //Intenta conectarse con los valores de las constantes ssid y pass a la red Wifi
  //Si la conexión falla el node se dormirá hasta que lo resetee
  while (WiFi.status() != WL_CONNECTED)
  {
    if ( WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_WRONG_PASSWORD ) {
      Serial.print("\nProblema con la conexión, revise los valores de las constantes ssid y pass");
      ESP.deepSleep(0);
    } else if ( WiFi.status() == WL_CONNECT_FAILED ) {
      Serial.print("\nNo se ha logrado conectar con la red, resetee el node y vuelva a intentar");
      ESP.deepSleep(0);
    }
    Serial.print(".");
    delay(1000);
  }
  Serial.println("connected!");

  //Sincroniza la hora del dispositivo con el servidor SNTP (Simple Network Time Protocol)
  Serial.print("Setting time using SNTP");
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < 1510592825) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  //Una vez obtiene la hora, imprime en el monitor el tiempo actual
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));

  #ifdef CHECK_CA_ROOT
    BearSSL::X509List cert(digicert);
    net.setTrustAnchors(&cert);
  #endif
  #ifdef CHECK_PUB_KEY
    BearSSL::PublicKey key(pubkey);
    net.setKnownKey(&key);
  #endif
  #ifdef CHECK_FINGERPRINT
    net.setFingerprint(fp);
  #endif
  #if (!defined(CHECK_PUB_KEY) and !defined(CHECK_CA_ROOT) and !defined(CHECK_FINGERPRINT))
    net.setInsecure();
  #endif
  net.setInsecure();
  //Llama a funciones de la librería PubSubClient para configurar la conexión con Mosquitto
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(receivedCallback);
  //Llama a la función de este programa que realiza la conexión con Mosquitto
  mqtt_connect();
}

//Función loop que se ejecuta indefinidamente repitiendo el código a su interior
//Cada vez que se ejecuta toma nuevos datos de la lectura del sensor
void loop()
{
  //Revisa que la conexión Wifi y MQTT siga activa
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("Checking wifi");
    while (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      WiFi.begin(ssid, pass);
      Serial.print(".");
      delay(10);
    }
    Serial.println("connected");
  }
  else
  {
    if (!client.connected())
    {
      mqtt_connect();
    }
    else
    {
      client.loop();
    }
  }

  now = time(nullptr);
  //Lee los datos del sensor
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  int rawLight = analogRead(A0);
  int lightPct = map(rawLight, 0, 1023, 0, 100);
  //Transforma la información a la notación JSON para poder enviar los datos 
  //El mensaje que se envía es de la forma {"value": x}, donde x es el valor de temperatura o humedad
  
  //JSON para humedad
  String json = "{\"value\": "+ String(h) + "}";
  char payload1[json.length()+1];
  json.toCharArray(payload1,json.length()+1);
  //JSON para temperatura
  json = "{\"value\": "+ String(t) + "}";
  char payload2[json.length()+1];
  json.toCharArray(payload2,json.length()+1);

  //Json para luminosidad (porcentaje + valor raw)
  String json3 = "{\"value\": " + String(lightPct) + "%, \"raw\": " + String(rawLight) + "}";
  char payload3[json3.length()+1];
  json3.toCharArray(payload3, json3.length()+1);

  // Publica luminosidad siempre (es analógica, no NaN)
  client.publish(MQTT_PUB_TOPIC3, payload3, false);

  //Si los valores recolectados no son indefinidos, se envían a los tópicos correspondientes
  if ( !isnan(h) && !isnan(t) ) {
    //Publica en el tópico de la humedad
    client.publish(MQTT_PUB_TOPIC1, payload1, false);
    //Publica en el tópico de la temperatura
    client.publish(MQTT_PUB_TOPIC2, payload2, false);

  }

  //Imprime en el monitor serial la información recolectada
  Serial.print(MQTT_PUB_TOPIC1);
  Serial.print(" -> ");
  Serial.println(payload1);
  Serial.print(MQTT_PUB_TOPIC2);
  Serial.print(" -> ");
  Serial.println(payload2);

  Serial.print(MQTT_PUB_TOPIC3);
  Serial.print(" -> ");
  Serial.println(payload3);
  /*Espera 5 segundos antes de volver a ejecutar la función loop*/
  delay(5000);
}

///////////////////  SEGUNDO CODIGO PARA MANEJAR EL LED A TRAVES DE UNA INTERFAZ WEB CON API

// #include <ESP8266WiFi.h>
// #include "DHT.h"
// #define DHTTYPE DHT11   // DHT 11

// const char* ssid = "ARIE";
// const char* password = "31379752305";

// #define LED D1 // LED
// // DHT Sensor
// uint8_t DHTPin = 4;

// // Initialize DHT sensor.
// DHT dht(DHTPin, DHTTYPE);

// float Temperature;
// float Humidity;

// WiFiServer server(80);

// void setup() {
//  Serial.begin(115200);
//  delay(10);

//  pinMode(LED, OUTPUT);
//  digitalWrite(LED, LOW); //LED apagado

//  // Connect to WiFi network
//  Serial.println();
//  Serial.println();
//  Serial.print("Connecting to ");
//  Serial.println(ssid);

//  WiFi.begin(ssid, password);

//  while (WiFi.status() != WL_CONNECTED) {
//    delay(500);
//    Serial.print(".");
//  }
//  Serial.println("");
//  Serial.println("WiFi connected");

//  // Start the server
//  server.begin();
//  Serial.println("Server started");

//  // Print the IP address
//  Serial.print("Use this URL to connect: ");
//  Serial.print("http://");
//  Serial.print(WiFi.localIP());
//  Serial.println("/");

// }

// void loop() {
//  Temperature = dht.readTemperature(); // Gets the values of the temperature
//  Humidity = dht.readHumidity(); // Gets the values of the humidity
//  // Check if a client has connected
//  WiFiClient client = server.available();
//  if (!client) {
//    return;
//  }

//  // Wait until the client sends some data
//  Serial.println("new client");
//  while(!client.available()){
//    delay(1);
//  }

//  // Read the first line of the request
//  String request = client.readStringUntil('\r');
//  Serial.println(request);
//  client.flush();

//  // Match the request

//  int value = HIGH;
//  if (request.indexOf("/LED=ON") != -1)  {
//    digitalWrite(LED, HIGH);
//    value = HIGH;
//  }
//  if (request.indexOf("/LED=OFF") != -1)  {
//    digitalWrite(LED, LOW);
//    value = LOW;
//  }

//  // Return the response
//  client.println("HTTP/1.1 200 OK");
//  client.println("Content-Type: text/html");
//  client.println(""); //  do not forget this one
//  client.println("<!DOCTYPE HTML>");
//  client.println("<html>");

//  client.print("LED is now: ");

//  if(value == LOW) {
//    client.print("Off");
//  } else {
//    client.print("On");
//  }
//  client.println("<br><br>");
//  client.println("<a href=\"/LED=ON\"\"><button>Turn On </button></a>");
//  client.println("<a href=\"/LED=OFF\"\"><button>Turn Off </button></a><br />");
//  client.println("</html>");

//  client.println("<br><br>");
//  client.print("Temperature: ");
//  client.print(Temperature);
//  client.print("C");
//  client.println("<br><br>");
//  client.print("Humidity: ");
//  client.print(Humidity);
//  client.print("%");

//  delay(1);
//  Serial.println("Client disconnected");
//  Serial.println("");

// }








////////////////////////////////   este codigo es para la primera parate del tutorial CON ENVIO DE 1 O 2 PARA PRENDER LED
// #include "DHT.h"
// #define DHTTYPE DHT11   // DHT 11

// #define dht_dpin 4
// DHT dht(dht_dpin, DHTTYPE);

// #define LED D1 // LED
// int ValueRead=2;
// int myflag=0;

// void setup()
// {
//  Serial.begin(9600);
//  pinMode(LED, OUTPUT);
//  digitalWrite(LED, LOW); //LED comienza apagado
// }

// void loop()
// {
//  if (Serial.available()){
//    ValueRead=Serial.parseInt();
//  }
//    if (((ValueRead==1 && myflag==0)|| myflag==1)&!(ValueRead==2 && myflag==1)){
//      digitalWrite(LED, HIGH);  // Se prende el LED
//      Serial.println("Prendido");
//      myflag=1;
//    }
//    else{
//      digitalWrite(LED, LOW);   // Se apaga el LED
//      Serial.println("Apagado");
//      myflag=0;
//    }
//  float h = dht.readHumidity();
//  float t = dht.readTemperature();
//  Serial.print("Current humidity = ");
//  Serial.print(h);
//  Serial.print("%  ");
//  Serial.print("temperature = ");
//  Serial.print(t);
//  Serial.println("C  ");
// }