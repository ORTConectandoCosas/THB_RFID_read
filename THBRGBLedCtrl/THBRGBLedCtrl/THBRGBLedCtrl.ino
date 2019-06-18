
// includes de bibliotecas para comunicación
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h> // instalar versión 6 de esta biblioteca

//***************MODIFICAR PARA SU PROYECTO *********************
//  configuración datos wifi 
// descomentar el define y poner los valores de su red y de su dispositivo
#define WIFI_AP "wifiname"
#define WIFI_PASSWORD "wifipass"


//  configuración datos thingsboard
#define NODE_NAME "LEDCTRL"   //nombre que le pusieron al dispositivo cuando lo crearon
#define NODE_TOKEN "thb token"   //Token que genera Thingboard para dispositivo cuando lo crearon


//***************NO MODIFICAR *********************
char thingsboardServer[] = "demo.thingsboard.io";

/*definir topicos.
 * telemetry - para enviar datos de los sensores
 * request - para recibir una solicitud y enviar datos 
 * attributes - para recibir comandos en baes a atributtos shared definidos en el dispositivo
 */
char telemetryTopic[] = "v1/devices/me/telemetry";
char requestTopic[] = "v1/devices/me/rpc/request/+";  //RPC - El Servidor usa este topico para enviar rquests, cliente response
char attributesTopic[] = "v1/devices/me/attributes";  //El Servidor usa este topico para enviar atributos

// declarar cliente Wifi y PubSus
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// declarar variables control loop (para no usar delay() en loop
unsigned long lastSend;
int elapsedTime = 1000; // tiempo transcurrido entre envios al servidor
const int MaxFlash = 20; 


//-------------------------------------------------------------
// LED Pins


int redpin = 16; // select the pin for the red LED = 1
int bluepin = 5; // select the pin for the blue LED
int greenpin = 4 ;// select the pin for the green LED

int ledPins[] = {redpin, bluepin, greenpin};

//enum LEDColors {RED, BLUE, GREEN} ledColor;
const int RED  = 0;
const int BLUE  = 1;
const int GREEN  = 2;
int activeLedPin;
int ledColor;

void setup() { 
  Serial.begin(9600);


  // inicializar wifi y pubsus
  connectToWiFi();
  client.setServer( thingsboardServer, 1883 );

  // agregado para recibir callbacks
  client.setCallback(on_message);
   
  lastSend = 0; // para controlar cada cuanto tiempo se envian datos

  // led setup
  pinMode (ledPins[RED], OUTPUT);
  pinMode (ledPins[BLUE], OUTPUT);
  pinMode (ledPins[GREEN], OUTPUT);
  ledColor = RED;


  digitalWrite(ledPins[ledColor],HIGH);
  activeLedPin = RED;
}
 
void loop() 
{

 if ( !client.connected() ) {
    reconnect();
  }

      
  client.loop();
}


void on_message(const char* topic, byte* payload, unsigned int length) 
{
    // Mostrar datos recibidos del servidor
  Serial.println("On message");
  char message[length + 1];
  strncpy ( message, (char*)payload, length);
   message[length] = '\0';
  
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println( message);

  // Verificar el topico por el cual llegó el mensaje, puede ser un cambio de atributos o un request
  if (strcmp(topic, "v1/devices/me/attributes") ==0) { //es un cambio en atributos compartidos
    Serial.println("----> CAMBIO DE ATRIBUTOS");
  } else {
    processRequest(message);
  }

}


//Metodo para procesar requests

/*  formato JSON
 * {
    "method": "Led",
    "params": {
        "Action": "ON",
        "Color": 0
    }
}
*/


void processRequest(char *message)
{

  // Decode JSON request with ArduinoJson 6 https://arduinojson.org/v6/doc/deserialization/
  // Notar que a modo de ejemplo este mensaje se arma utilizando la librería ArduinoJson en lugar de desarmar el string a "mano"
  
  const int capacity = JSON_OBJECT_SIZE(4);
  StaticJsonDocument<capacity> doc;
  DeserializationError err = deserializeJson(doc, message);
  
  if (err) {
    Serial.print(("deserializeJson() failed with code "));
    Serial.println(err.c_str());
    return;
  }

  String method = doc["method"];
  String action = doc["params"]["Action"];
  int ledPin = doc["params"]["Color"];

 
    Serial.print("Action RCV:");
    Serial.print(action);
    
    if (action.equals("ON")) {
      setLedOn(ledPin);
    } else {
      flashLED(ledPin);
    } 
 
}

void setLedOn(int ledPin)
{
  if (activeLedPin != ledPin) {
     digitalWrite(ledPins[activeLedPin],LOW);
  } else {
    Serial.print("ON LED on pin:");
    Serial.println(ledPin);
    digitalWrite(ledPins[ledPin],HIGH);
    activeLedPin = ledPin;
  }
}
void flashLED(int ledPin)
{
  Serial.print("FLASH LED on pin:");
  Serial.println(ledPin);
  for (int i =0; i < MaxFlash; i++) {
      digitalWrite(ledPins[ledPin],HIGH);
      delay(50);
      digitalWrite(ledPins[ledPin],LOW);
      delay(50);
  }
}
//***************NO MODIFICAR - Conexion con Wifi y ThingsBoard *********************
/*
 * funcion para reconectarse al servidor de thingsboard y suscribirse a los topicos de RPC y Atributos
 */
void reconnect() {
  int statusWifi = WL_IDLE_STATUS;
  // Loop until we're reconnected
  while (!client.connected()) {
    statusWifi = WiFi.status();
    connectToWiFi();
    
    Serial.print("Connecting to ThingsBoard node ...");
    // Attempt to connect (clientId, username, password)
    if ( client.connect(NODE_NAME, NODE_TOKEN, NULL) ) {
      Serial.println( "[DONE]" );
      
      // Suscribir al Topico de request
      client.subscribe(requestTopic); 
      client.subscribe(attributesTopic); 
            
    } else {
      Serial.print( "[FAILED] [ rc = " );
      Serial.print( client.state() );
      Serial.println( " : retrying in 5 seconds]" );
      // Wait 5 seconds before retrying
      delay( 5000 );
    }
  }
}

/*
 * función para conectarse a wifi
 */
void connectToWiFi()
{
  Serial.println("Connecting to WiFi ...");
  // attempt to connect to WiFi network

  WiFi.begin(WIFI_AP, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
}
