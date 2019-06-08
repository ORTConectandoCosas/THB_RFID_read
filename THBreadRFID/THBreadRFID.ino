//Ejemplo adaptado de https://www.teachmemicro.com/arduino-rfid-rc522-tutorial/
// pin NodeMCU  en https://github.com/miguelbalboa/rfid o  https://www.instructables.com/id/MFRC522-RFID-Reader-Interfaced-With-NodeMCU/

#include <SPI.h>
#include <MFRC522.h>

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
// LED Pins
int redpin = D0; // select the pin for the red LED
int bluepin = D1; // select the pin for the blue LED
int greenpin = D8 ;// select the pin for the green LED


void setup() { 
  Serial.begin(9600);

  //RFID communication and initialization
  initRFIDReader();

  // led setup
  pinMode (redpin, OUTPUT);
  pinMode (bluepin, OUTPUT);
  pinMode (greenpin, OUTPUT);
}
 
void loop() {

  // read UID
  if (readRFIDCard() == 0) {
      Serial.print("UID:" );
      
      Serial.println(uidString);
  }
}



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
//        -1 - no card present
//         -2 - no reading
int readRFIDCard() {
    // Look for new cards
  if ( ! rfid.PICC_IsNewCardPresent())
    return -1;

  // Verify if the NUID has been readed
  if ( ! rfid.PICC_ReadCardSerial())
    return - 2;

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
