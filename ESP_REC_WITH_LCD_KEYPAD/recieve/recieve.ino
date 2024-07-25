#include <WiFi.h>
#include <esp_now.h>
#include <LiquidCrystal.h>
#include <Keypad.h>


#define RXp2 16
#define TXp2 17

#define MAX_BUFFER_SIZE 140 // Change this to the maximum size of your arrays
#define NUM_ARRAYS 100
uint8_t receivedArrays[NUM_ARRAYS][MAX_BUFFER_SIZE];

void write() {

  int z=0;
  for( z=0;z<=46;z++) 
  {
  Serial2.write(receivedArrays[z][0]);
  //Serial.println("ssssss");
  
  delay(10);
  for(int i=1;i<=138;i++)
  {
    Serial2.write(receivedArrays[z][i]);
    delay(10);
   // Serial.print(receivedArrays[z][i]);
  }
  //Serial.println("BEFORE WHILE");
  //while(Serial2.read()!=0xAA);
  delay(100);
  
  Serial.println(z);
  }

}

uint8_t erase_Command[]={0x15 ,  0xff,  0x00, 0x00, 0x00, 0x00, 0x20,  0x61,  0xcd,  0x43};
void earse_App()
{
  /* send the length of the packet*/
  Serial2.write(0x0a);
  delay(100);
  /* send the Packet */
  for(int i=0;i<10;i++)
  {
    Serial2.write(erase_Command[i]);
    delay(10);
  }
  //while(Serial2.read()!=0xAA);
  //Serial.println("finish earase");
}

void Jump_to_app()
{
   Serial2.write(4);
   delay(10);
}




// Define the keypad layout
const byte ROWS = 4; // Number of rows
const byte COLS = 4; // Number of columns
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte pin_rows[ROWS]      = {13, 12, 14, 27}; // GPIO19, GPIO18, GPIO5, GPIO17 connect to the row pins
byte pin_column[COLS] = {26, 25, 21, 32};     // Connect to the column pinouts of the keypad

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROWS, COLS);

LiquidCrystal lcd(15, 2, 0,5,18,19);

const String savedPassword = "7286"; // Change this to your desired password

// Callback function executed when data is received
int k=0,j=0,flag=0;
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Recieving");
  
  j=0;
  for(int j=0 ; j<= 138 ; j++)
  { 
    Serial.print(incomingData[j] );
    receivedArrays[k][j]=incomingData[j];
    Serial.print(" ");
  }      

  k++;
  flag++;
  Serial.println(" "); 
}

void enter_pass()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter password:");
  lcd.setCursor(0, 1);

  String enteredPassword = "";
  while (enteredPassword.length() < 4) {
    char key = keypad.getKey();

    if (key) {
      enteredPassword += key;
      lcd.print("*");
    }
  }

  delay(500); // Wait for stability

  if (enteredPassword == savedPassword) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access granted!");
    // Perform actions when the password is correct
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access denied!");
    // Perform actions when the password is incorrect
  }

  delay(2000); // Delay before clearing the LCD and asking for password again
  lcd.clear();
}


void setup() {
  // Initialize arrays with data
  Serial.begin(115200);
  //initWiFi();
  Serial2.begin(115200, SERIAL_8N1, RXp2, TXp2);

  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Welcome Car 2");
  // Do something with the result array...
  // Initilize ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callback function
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {

  if(flag>=48)
  {
    Serial.println(" ");
    delay(100);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("New update!");
    delay(1000);
    enter_pass();
    earse_App();
    delay(1000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Updating...!");
    write();
    Serial.println("fINISH WRITING");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Finished!");
    delay(1000);
    Jump_to_app();
    Serial.println("JUMPED");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Welcome Car 2");
    flag=0;
  }
}



