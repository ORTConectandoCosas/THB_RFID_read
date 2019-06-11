
// includes de bibliotecas para comunicación
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h> // instalar versión 6 de esta biblioteca

//***************MODIFICAR PARA SU PROYECTO *********************
//  configuración datos wifi 
// descomentar el define y poner los valores de su red y de su dispositivo
#define WIFI_AP "NOMBRE_RED"
#define WIFI_PASSWORD "PASSWORD_RED"


//  configuración datos thingsboard
#define NODE_NAME "Nombre dispositivo THB"   //nombre que le pusieron al dispositivo cuando lo crearon
#define NODE_TOKEN "Token dispositivo THB"   //Token que genera Thingboard para dispositivo cuando lo crearon


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



//-------------------------------------------------------------
// LED Pins
int redpin = D0; // select the pin for the red LED = 1
int bluepin = D1; // select the pin for the blue LED
int greenpin = D2 ;// select the pin for the green LED
enum LEDColors {RED, BLUE, GREEN} ledColor;


void setup() { 
  Serial.begin(9600);


  // inicializar wifi y pubsus
  connectToWiFi();
  client.setServer( thingsboardServer, 1883 );

  // agregado para recibir callbacks
  client.setCallback(on_message);
   
  lastSend = 0; // para controlar cada cuanto tiempo se envian datos

  // led setup
  pinMode (redpin, OUTPUT);
  pinMode (bluepin, OUTPUT);
  pinMode (greenpin, OUTPUT);
  ledColor = RED;
  
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
 //   processAttributeRequestCommand(message);
  } else {
    // es un request
    Serial.println("----> REQUEST");
    processRequest(message);
  }

}


//Metodo para procesar requests
void processRequest(char *message)
{

  // Decode JSON request with ArduinoJson 6 https://arduinojson.org/v6/doc/deserialization/
  // Notar que a modo de ejemplo este mensaje se arma utilizando la librería ArduinoJson en lugar de desarmar el string a "mano"
  
  const int capacity = JSON_OBJECT_SIZE(4);
  StaticJsonDocument<capacity> doc;
  DeserializationError err = deserializeJson(doc, message);
  
  if (err) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(err.c_str());
    return;
  }

  String method = doc["method"];
  int ledIndex = doc["params"];
  switch(ledIndex) {
    case RED:
      digitalWrite(redpin,HIGH);
      delay(1000);
      digitalWrite(redpin,LOW); 
      break;
    case BLUE:
      digitalWrite(bluepin,HIGH);
      delay(1000);
      digitalWrite(bluepin,LOW); 
      break ;
    case GREEN:
      digitalWrite(greenpin,HIGH);
      delay(1000);
      digitalWrite(greenpin,LOW); 
      break ;
     default:
        digitalWrite(redpin,HIGH);
        break;
     
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
