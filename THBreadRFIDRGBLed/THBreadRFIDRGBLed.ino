//Ejemplo adaptado de https://www.teachmemicro.com/arduino-rfid-rc522-tutorial/
// pin NodeMCU  en https://github.com/miguelbalboa/rfid o  https://www.instructables.com/id/MFRC522-RFID-Reader-Interfaced-With-NodeMCU/

#include <SPI.h>
#include <MFRC522.h>

// includes de bibliotecas para comunicación
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h> // instalar versión 6 de esta biblioteca

//***************MODIFICAR PARA SU PROYECTO *********************
//  configuración datos wifi 
// descomentar el define y poner los valores de su red y de su dispositivo
#define WIFI_AP "SSID"
#define WIFI_PASSWORD "pass"


//  configuración datos thingsboard
#define NODE_NAME "Nombre dispositivo THB"   //nombre que le pusieron al dispositivo cuando lo crearon
#define NODE_TOKEN "Token dispositivo THB"   //Token que genera Thingboard para dispositivo cuando lo crearon


//***************NO MODIFICAR *********************
char thingsboardServer[] = "demo.thingsboard.io";

/*definir topicos.
 * telemetry - use it to send telemetry
 * request -  use to receive and send requests to server
 * attributes - to receive commands from server to device
 */
char telemetryTopic[] = "v1/devices/me/telemetry";
char requestTopic[] = "v1/devices/me/rpc/request/+";  //RPC - El Servidor usa este topico para enviar rquests, cliente response
char attributesTopic[] = "v1/devices/me/attributes";  //El Servidor usa este topico para enviar atributos

// declarar cliente Wifi y PubSus
WiFiClient wifiClient;
PubSubClient client(wifiClient);


//-------------------------------------------------------------
//RFID Reader Pins and setup
#define SS_PIN D4
#define RST_PIN D3
 
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class

MFRC522::MIFARE_Key key; 

// Init array that will store new NUID 
byte nuidPICC[4];

char uidString[9]; // 4 x 2 chars for the 4 bytes + trailing '\0'

//-------------------------------------------------------------
// Avoidance 
int avoidancePin = D1;

//Led Colors
enum LEDColors {RED, BLUE, GREEN} ledColor;

// app logic state of userRead, userAuthenticated and bottleDetected
int userCardRead = -2; // 0 - read, -1 ==  not read
bool userAuthenticated = false;
bool userCredited = false;
bool bottleDetected = false;
bool transactionInProcess = false;
bool serverRequestInProgress = false;


// declarar variables control loop (para no usar delay() en loop
unsigned long lastSend;
int elapsedTime = 1000; // tiempo transcurrido entre envios al servidor
int requestNumber =1;

void setup() { 
  Serial.begin(9600);

  // init wifi y pubsus
  connectToWiFi();
  client.setServer(thingsboardServer, 1883);

  // setup callbacks
  client.setCallback(on_message);
   
  lastSend = 0; // variable to ctrl delays

  //RFID communication and initialization
  initRFIDReader();

  pinMode(avoidancePin, INPUT);
}
 
void loop() 
{
    if ( !client.connected() ) {
    reconnect();
    }

    if (transactionInProcess == false) {
      userCardRead = readRFIDCard();  // read card
      transactionInProcess = true;
    } else {
      if (userCardRead == 0) { // ok user card read
          Serial.print("UID:" );
          Serial.println(uidString);
    
          // call server for authentication
          requestUserAuthentication(uidString);
    
          // note: userAuthenticated variable is set by the server response on methos on_message
          if(userAuthenticated) {
            Serial.println("user ok");
    
            // call server to set led
            requestToLedDevice(GREEN, "ON");
            
            // check if bottle drop sensor
            bottleDetected = detectBottle();
    
            if (bottleDetected) {
              Serial.println("bottle ok");
              // flash to signal bottle ok
              requestToLedDevice(GREEN, "FLASH");

              requestCreditToUser(uidString);

              // note: userCredited variable is set by the server response on methos on_message
              if (userCredited) {
                Serial.println("user credited");
                 transactionInProcess = false;
              } else {
                Serial.println("waiting for user credit");
              }
            } else {
                Serial.println("bottle not detected");
            }
          } else {
             Serial.println("waiting for user auth");
          }
      }
    }
  client.loop();
}


void  requestToLedDevice(LEDColors led, String action)
{

}

// server RPC Calls
void requestUserAuthentication(char *)
{    
  if (serverRequestInProgress == false) {
    serverRequestInProgress = true;
    userAuthenticated = false;
    
    const int capacity = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<capacity> doc;
    doc["method"] = "checkUser";
    doc["user"] = uidString;
    String output = "";
    serializeJson(doc, output);
    
    Serial.print("json to send:");
    Serial.println(output);

   char attributes[100];
    output.toCharArray( attributes, 100 );
   if (client.publish( requestTopic, attributes ) == true)
    Serial.println("publicado ok");
    else {
        serverRequestInProgress = false;
    }
        
  }
}


void requestCreditToUser(char *)
{
   if (serverRequestInProgress == false) {
    serverRequestInProgress = true;
    const int capacity = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<capacity> doc;
    doc["method"] = "creditUser";
    doc["user"] = uidString;
    
    String output = "";
    serializeJson(doc, output);
    
    Serial.print("json to send:");
    Serial.println(output);

   char attributes[100];
    output.toCharArray( attributes, 100 );
   if (client.publish( requestTopic, attributes ) == true)
    Serial.println("publicado ok");
    else {
       serverRequestInProgress = false;
    }
   }
}



/*
 * Thingsboard methods
 */
void on_message(const char* topic, byte* payload, unsigned int length) 
{
    // print received message
  Serial.println("On message");
  char message[length + 1];
  strncpy ( message, (char*)payload, length);
   message[length] = '\0';
  
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println( message);

  // Check topic for attributes o request
  if (strcmp(topic, "v1/devices/me/attributes") ==0) { //es un cambio en atributos compartidos
    Serial.println("----> CAMBIO DE ATRIBUTOS");
    //processAttributeRequestCommand(message);
  } else {
    // request
    Serial.println("----> REQUEST");
    processRequest(message);
  }

}

// Process request
void processRequest(char *message)
{

  // Decode JSON request with ArduinoJson 6 https://arduinojson.org/v6/doc/deserialization/
  // Notar que a modo de ejemplo este mensaje se arma utilizando la librería ArduinoJson en lugar de desarmar el string a "mano"
  
  const int capacity = JSON_OBJECT_SIZE(4);
  StaticJsonDocument<capacity> doc;
  DeserializationError err = deserializeJson(doc, message);
  
  if (err) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(err.c_str());\
    return;
  }

  serverRequestInProgress = false;
    
  String methodName = doc["method"];
  if (methodName.equals("userAth")) {
    bool authResponse = doc["params"];
    userAuthenticated = authResponse;

  } else if (methodName.equals("userCredited")) {
    bool creditResponse = doc["params"];
    userCredited = creditResponse;
  } 
}

/* 
 *  Solution Sensor management functions
 *
 */
 
bool detectBottle()
{
    return (digitalRead(avoidancePin) == LOW ? true: false);
}



/*
 * RFID Methods
 */
void initRFIDReader()
{
  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522 

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

// read RFID Card 
// return 0- ok - UID in nuidPICC
//         -1 - no reading
int readRFIDCard() {
    // Look for new cards
  if ( ! rfid.PICC_IsNewCardPresent())
    return -1;

  // Verify if the NUID has been readed
  if ( ! rfid.PICC_ReadCardSerial())
    return - 1;

  // copy UID to nuidPICC
 for (byte i = 0; i < 4; i++) {
      nuidPICC[i] = rfid.uid.uidByte[i];
    }

   for (byte i = 0; i < 4; i++) 
    storeHexRepresentation(&uidString[2 * i], nuidPICC[i]);

   rfid.PICC_HaltA();
   rfid.PCD_StopCrypto1();

   return 0;
}


// store on 2 char the Hex represnetation of byte v
// adds a trailing '\0'
// so b should point to an array with at least 3 bytes available to contain the representation
void storeHexRepresentation(char *b, const byte v)
{
  if (v <= 0xF) {
    *b = '0';
    b++;
  }
  itoa(v, b, 16); // http://www.cplusplus.com/reference/cstdlib/itoa/
}

//***************NO MODIFICAR - Conexion con Wifi y ThingsBoard *********************
/*
 * THB connection and topics
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
 * 
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
