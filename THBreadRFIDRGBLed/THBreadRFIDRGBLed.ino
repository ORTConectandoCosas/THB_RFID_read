//Ejemplo adaptado de https://www.teachmemicro.com/arduino-rfid-rc522-tutorial/
// pin NodeMCU  en https://github.com/miguelbalboa/rfid o  https://www.instructables.com/id/MFRC522-RFID-Reader-Interfaced-With-NodeMCU/

#include <SPI.h>
#include <MFRC522.h>

// includes comunicación libs
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h> // check if you are using versión 6 

//***************MODIFICAR PARA SU PROYECTO *********************
//  configuración datos wifi 
// descomentar el define y poner los valores de su red y de su dispositivo
#define WIFI_AP "WIFNAME"
#define WIFI_PASSWORD "wifiPASS"


//  configuración datos thingsboard
#define NODE_NAME "THBDEV"   //nombre que le pusieron al dispositivo cuando lo crearon
#define NODE_TOKEN "THBTOKEN"   //Token que genera Thingboard para dispositivo cuando lo crearon


//***************NO MODIFICAR *********************
char thingsboardServer[] = "demo.thingsboard.io";

/*definir topicos.
 * telemetry - use to send telemetry
 * request -  use to receive and send requests to server
 * attributes - to receive commands from server to device
 */
char telemetryTopic[] = "v1/devices/me/telemetry";
char requestTopic[] = "v1/devices/me/rpc/request/+";  //RPC - El Servidor usa este topico para enviar rquests, cliente response
char responseTopic[] = "v1/devices/me/rpc/response/+"; // RPC responce
char attributesTopic[] = "v1/devices/me/attributes";  //El Servidor usa este topico para enviar atributos


// declarar cliente Wifi y PubSus
WiFiClient wifiClient;
PubSubClient client(wifiClient);


//-------------------------------------------------------------
//RFID Reader Pins and setup
//-------------------------------------------------------------
#define SS_PIN D4
#define RST_PIN D3
 
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class

MFRC522::MIFARE_Key key; 

// Init array that will store new UID and String to store UID
byte nuidPICC[4];
char uidString[9]; // 4 x 2 chars for the 4 bytes + trailing '\0'

//-------------------------------------------------------------
// Avoidance sensor
//-------------------------------------------------------------
int avoidancePin = D1;
bool bottleDetected = false;

//-------------------------------------------------------------
//Led colors
enum LEDColors {RED, BLUE, GREEN} ledColor;
//-------------------------------------------------------------

//-------------------------------------------------------------
// app logic state of userRead, userAuthenticated and bottleDetected
int userCardRead = -2; // 0 - read, -1 ==  not read
bool userAuthenticated = false;
bool userCredited = false;
enum transactionSteps {READCARD, AUTH, WAIT_AUTH, DETECTBTL, CREDITUSER, WAIT_CREDITUSER, END};
transactionSteps transactionInProcess = READCARD;

//-------------------------------------------------------------
// THB request timer variables
//-------------------------------------------------------------
unsigned long lastSend;
int elapsedTime = 3000; // elapsed time request vs reply
int requestNumber =1;  
bool serverRequestInProgress = false;

// App Logic
void setup() { 
  Serial.begin(9600);

  // init wifi & pubsus callbacks
  Serial.println("init connection"),
  
  connectToWiFi();
  delay(10);
  
  client.setServer(thingsboardServer, 1883);
  client.setCallback(on_message);
   
  lastSend = 0; // variable to ctrl delays
      
  //RFID communication and initialization
  initRFIDReader();
  Serial.print("Init RFID ...");
      
  pinMode(avoidancePin, INPUT);

  Serial.print("STATE:");
  Serial.println(transactionInProcess);
}
 
void loop() 
{  
    if ( !client.connected() ) {
    reconnect();
    transactionInProcess = READCARD;
    stopServerRequestTimer();
    }
    
    if (transactionInProcess == READCARD) {
      userCardRead = readRFIDCard();  // read card
      if (userCardRead ==0) {
        Serial.println("Card ok");
        transactionInProcess = AUTH;
      }
    }
    
    if (transactionInProcess == AUTH) {
          // call server for authentication
          requestUserAuthentication(uidString);

          Serial.println("Request auth");
          transactionInProcess = WAIT_AUTH;
          
      }

    if (transactionInProcess == WAIT_AUTH) {
          // note: async userAuthenticated variable is set by the server response on methods called by on_message()
          if(userAuthenticated) {
            Serial.println("user ok");
            transactionInProcess = DETECTBTL;
          }
      }

    if (transactionInProcess == DETECTBTL) {
            // call server to set led
            //requestToLedDevice(GREEN, "ON");
            
            // check if bottle drop sensor
            bottleDetected = detectBottle();
    
            if (bottleDetected) {
              Serial.println("bottle ok");
              // flash to signal bottle ok
              //requestToLedDevice(GREEN, "FLASH");
              transactionInProcess = CREDITUSER;
            }
    }
    
    if (transactionInProcess == CREDITUSER) {
              requestCreditToUser(uidString);

              Serial.println("Request credited for user");
              transactionInProcess = WAIT_CREDITUSER;
        
    }

    if (transactionInProcess == WAIT_CREDITUSER) {
              // note: async userCredited variable is set by the server response on methods called by on_message()
              if (userCredited) {
                Serial.println("user credited");
                 transactionInProcess = END;
              } 
    }

    if (transactionInProcess == END) {
       transactionInProcess = READCARD;
       userCredited = false;
       bottleDetected = false;
       userAuthenticated = false;
       Serial.println("-------- END ------------");
    }
 
  // check for server timeout
  checkRequestInProgressTimeout();
 
  client.loop();

}

// server RPC Request only
void  requestToLedDevice(LEDColors ledColor, String action)
{
    const int capacity = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<capacity> doc;
    doc["LedBehaviour"] = action;
    doc["Color"] = ledColor;
    
    String output = "";
    serializeJson(doc, output);
    
    Serial.print("json to send:");
    Serial.println(output);

   char attributes[100];
   output.toCharArray( attributes, 100 );
   if (client.publish( requestTopic, attributes ) == true)
    Serial.println("publicado ok");
}

// server RPC Request only & Reply (see on_message)
void requestUserAuthentication(char *)
{    
  if (isServerRequestTimerInProgress() == false) {
    startServerRequestTimer();
    
    const int capacity = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<capacity> doc;
    doc["method"] = "checkUserID";
    doc["params"] = uidString;
    String output = "";
    serializeJson(doc, output);
    
    char attributes[100];
    output.toCharArray( attributes, 100 );
    
    requestNumber++;
    
    String requestCmnd = String("v1/devices/me/rpc/request/") + String(requestNumber);

    Serial.print("TOPIC:");
    Serial.print(requestCmnd);
    Serial.print("  --- >json to send:");
    Serial.println(output);
    
   if (client.publish(requestCmnd.c_str() , attributes ) == true) {
      Serial.println("publicado ok");
    } else {
       Serial.println("publicado ERROR");
       stopServerRequestTimer();
    }     
  }
}

// server RPC Request only & Reply (see on_message)
void requestCreditToUser(char *)
{
   if (isServerRequestTimerInProgress() == false) {
    startServerRequestTimer(); 
    
    const int capacity = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<capacity> doc;
    doc["method"] = "creditPointsToUser";
    doc["params"] = uidString;
    
    String output = "";
    serializeJson(doc, output);
    
    Serial.print("json to send:");
    Serial.println(output);

   char attributes[100];
    output.toCharArray( attributes, 100 );
    requestNumber++;
    
    String requestCmnd = String("v1/devices/me/rpc/request/") + String(requestNumber);

    Serial.print("TOPIC:");
    Serial.print(requestCmnd);
    Serial.print("  --- >json to send:");
    Serial.println(output);
    
   if (client.publish(requestCmnd.c_str() , attributes ) == true) {
      Serial.println("publicado ok");
    } else {
       Serial.println("publicado ERROR");
       stopServerRequestTimer();
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

  //message received stop timer
  stopServerRequestTimer();
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
    Serial.print(("deserializeJson() failed with code "));
    Serial.println(err.c_str());
    
    return;
  }
    
  String methodName = doc["method"];
  if (methodName.equals("checkUserID")) {
    bool authResponse = doc["params"];
    userAuthenticated = true;

  } else if (methodName.equals("creditPointsToUser")) {
    bool creditResponse = doc["params"];
    userCredited = true;
  } 
}

// start requestInProgress timer
void startServerRequestTimer()
{
    serverRequestInProgress = true;
    lastSend = millis();   //set timer
}

// stop requestInProgress timer
void stopServerRequestTimer()
{
    serverRequestInProgress = false;
}

// check requestInProgress timer status
bool isServerRequestTimerInProgress()
{
  return (serverRequestInProgress);
}

// check requestInProgress for timeout
void checkRequestInProgressTimeout()
{
  if ((millis() - lastSend > elapsedTime) && isServerRequestTimerInProgress() == true) { // Update and send only after 1 seconds
    Serial.println("-> Server RPC timeout");
    stopServerRequestTimer();
  }

}


/* 
 *  Solution Sensor management functions
 *
 */
 
//Check Avoidance sensor for bottle
bool detectBottle()
{
    return true;
    //return (digitalRead(avoidancePin) == LOW ? true: false);
}



/*
 * RFID Methods
 */
void initRFIDReader()
{
  Serial.println("RFID Reader Init start"),
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
  if ( ! rfid.PICC_IsNewCardPresent()) {
      return -1;  
  }


  // Verify if the NUID has been readed
  if ( ! rfid.PICC_ReadCardSerial()) {
          Serial.println("RFID Reader CARD ERROR 2");
      return -1; 
  }


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
 * THB connection and topics subscription
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
      client.subscribe(responseTopic);     
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
 *  wifi connection
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
