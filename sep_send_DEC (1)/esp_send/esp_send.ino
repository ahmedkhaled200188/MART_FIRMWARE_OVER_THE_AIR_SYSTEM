#include <LiquidCrystal.h>
#include <Keypad.h>
#include "FS.h"
#include <AESLib.h>        // Include the AESLib library for AES encryption
#include <stdint.h>
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <LittleFS..h>
#include <esp_now.h>
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"
#include <WiFi.h>
#include <WebServer.h>

#define API_KEY "AIzaSyC1T33X-TFgmt9LfCcqdp887FFPRTVs0jI"
#define DATABASE_URL "https://fota-2024-default-rtdb.firebaseio.com/"
#define STORAGE_BUCKET_ID "fota-2024.appspot.com"
#define WIFI_SSID "FOTA-2024"
#define WIFI_PASSWORD "123456789"
#define MAX_BUFFER_SIZE 140 // Change this to the maximum size of your arrays
#define NUM_ARRAYS 100
#define RXp2 16
#define TXp2 17

uint8_t myarr[NUM_ARRAYS][MAX_BUFFER_SIZE];
uint8_t broadcastAddress[] = {0xC0, 0x49, 0xEF, 0xF9, 0xDC, 0x98};
bool taskCompleted = false;

WebServer server(80);

// AES encryption key (16 bytes)
uint8_t aes_key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,0xab, 0xf7, 0x97, 0x27, 0x5d, 0x8b, 0x3e, 0x2b};
// Initialization vector (IV) for AES encryption (16 bytes)
uint8_t aes_iv[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
AESLib aesLib;

bool last_packet = false;
int counter=0;
uint32_t address = 0x08008000;

#define CHUNK_SIZE 128 // Adjust the chunk size as needed
uint8_t resultArray[MAX_BUFFER_SIZE];

esp_now_peer_info_t peerInfo; 
// Callback function called when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  //Serial.print("\r\nLast Packet Send Status:\t");
  //Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}
 


void initwifi2() {
  // Set ESP32 as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  // Initilize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the send callback
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
}
void deinitWiFi() {
  // Disconnect from Wi-Fi
  WiFi.disconnect(true);
  delay(1000);  // Give some time to complete disconnection

  // Optionally, you can turn off the Wi-Fi module
  WiFi.mode(WIFI_OFF);

  // Clear stored WiFi credentials
  WiFi.begin("", "");

  // Additional cleanup or resource release if needed

  Serial.println("Wi-Fi deinitialized");
}
void initWiFi() {
    // Set ESP32 as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
    taskCompleted = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}


uint32_t Calculate_CRC32(uint8_t* Buffer, size_t Buffer_Length) {
  uint32_t CRC_Value = 0xFFFFFFFF;
  for (size_t i = 0; i < Buffer_Length; i++) {
    CRC_Value = CRC_Value ^ Buffer[i];
    for (int j = 0; j < 32; j++) {
      if (CRC_Value & 0x80000000) {
        CRC_Value = (CRC_Value << 1) ^ 0x04C11DB7;
      } else {
        CRC_Value = (CRC_Value << 1);
      }
    }
  }
  CRC_Value = CRC_Value & 0xFFFFFFFF;
  return CRC_Value;
}

int last_data_size;
int last_packet_size;
int last_packet_round;
// The Firebase Storage download callback function
void fcsDownloadCallback(FCS_DownloadStatusInfo info)
{
  
    if (info.status == firebase_fcs_download_status_init)
    {
        Serial.printf("Downloading file %s (%d) to %s\n", info.remoteFileName.c_str(), info.fileSize, info.localFileName.c_str());
        int downloaded_file_size;
        int result;

        downloaded_file_size=info.fileSize;
        result = downloaded_file_size/128;
        last_packet_round=result+1;
        result =result*128; 
        last_data_size = downloaded_file_size-result;
        last_packet_size= last_data_size+10;
        Serial.println();
        Serial.println(last_packet_round);
        Serial.println(last_data_size);
        Serial.println(last_packet_size);
    }
    else if (info.status == firebase_fcs_download_status_download)
    {
        Serial.printf("Downloaded %d%s, Elapsed time %d ms\n", (int)info.progress, "%", info.elapsedTime);
    }
    else if (info.status == firebase_fcs_download_status_complete)
    {
        Serial.println("Download completed\n");
    }
    else if (info.status == firebase_fcs_download_status_error)
    {
        Serial.printf("Download failed, %s\n", info.errorMsg.c_str());
    }
    
}

void combineArraysAndCRC( uint8_t* array2, size_t array2Size, uint8_t* resultArray) {
  uint8_t data_length;
  uint8_t length;
  
  if(last_packet)
  {
    length= last_packet_size;
    data_length= last_data_size;

  }else
  {
     length = 0x8a;
     data_length= 0x80;
  }
  
  resultArray[0]=length;
  resultArray[1]= 0x16;
  resultArray[2]= address & 0xFF;
  resultArray[3]=(address >> 8) & 0xFF;
  resultArray[4]=(address >> 16) & 0xFF;
  resultArray[5]=(address >> 24) & 0xFF;
  resultArray[6]=data_length;

  address= address+0x80;
  size_t i = 7;

  // Copy the second array into the result array
  for (size_t j = 0; j < array2Size; j++, i++) {
    resultArray[i] = array2[j];
  }

  // Calculate CRC for the combined arrays
  uint32_t combinedCRC = Calculate_CRC32(resultArray, data_length + 7);
  // Copy the combined CRC into the result array
  resultArray[i++] = combinedCRC & 0xFF;
  resultArray[i++] = (combinedCRC >> 8) & 0xFF;
  resultArray[i++] = (combinedCRC >> 16) & 0xFF;
  resultArray[i] = (combinedCRC >> 24) & 0xFF;
}




void send_v2v(const char* filename)
{
    File file = LittleFS.open(filename, "rb");
  if (file) {
    Serial.println("File opened successfully");
    uint8_t chunk[CHUNK_SIZE];
    size_t bytesRead;
    int i=0;
    // Read and process the file in chunks
    while ((bytesRead = file.read(chunk, CHUNK_SIZE)) > 0) {
      //Serial.println("File content:");
      size_t chunk_Size = sizeof(chunk);
      

      combineArraysAndCRC( chunk, chunk_Size, resultArray);
      Serial.print("HEREEEE ");
      
      
      memcpy(myarr[i], resultArray, sizeof(resultArray));

      
      i++;
    }
}
}

//Function to decrypt the downloaded file 
//Function to decrypt the downloaded file 
void decryptFile(const char* inputPath, const char* outputPath) {
  File inputFile = LittleFS.open(inputPath, "r");
  if (!inputFile) {
    Serial.println("Failed to open encrypted file for reading");
    return;
  }

  File outputFile = LittleFS.open(outputPath, "w");
  if (!outputFile) {
    Serial.println("Failed to open decrypted file for writing");
    return;
  }

  int bufferSize = 1024; // Adjust as needed
  byte buffer[bufferSize];
  size_t length;
  bool skipInitialBytes = true;
  byte lastBlock[4]; // Buffer to hold the last potential 4 characters to check at EOF
  int lastBlockLength = 0; // Length of the actual last block that might be less than 4


  aesLib.gen_iv(aes_iv);
  aesLib.set_paddingmode((paddingMode)0); // AES_PKCS7_PADDING

    while ((length = inputFile.readBytes((char*) buffer, bufferSize)) > 0) {
    byte decrypted[length];
    int decrypted_length = aesLib.decrypt((byte*) buffer, length, (byte*) decrypted, aes_key, 128, aes_iv);

    if (skipInitialBytes) {
      if (decrypted_length > 16) {
        memcpy(decrypted, decrypted + 16, decrypted_length - 16);
        decrypted_length -= 16;
        skipInitialBytes = false; // Reset the flag after skipping
      } else {
        // If the first decrypted chunk is less than or equal to 12 bytes, skip the entire chunk
        continue;
      }
    }

    if (decrypted_length <= 4) {
      // Copy potential last block to buffer
      memcpy(lastBlock, decrypted, decrypted_length);
      lastBlockLength = decrypted_length;
    } else {
      if (lastBlockLength > 0) {
        // Write the previous last block except the last 4 bytes
        outputFile.write(lastBlock, lastBlockLength - 4);
        lastBlockLength = 0;
      }
      // Update the last block with the last 4 bytes of the current decrypted chunk
      memcpy(lastBlock, decrypted + decrypted_length - 4, 4);
      lastBlockLength = 4;
      // Write current decrypted data except the last 4 bytes
      outputFile.write(decrypted, decrypted_length - 4);
    }
  }

  // Handle the final block if its length is greater than 4
  if (lastBlockLength > 4) {
    outputFile.write(lastBlock, lastBlockLength - 4);
  }

  inputFile.close();
  outputFile.close();
}

void read_binary_file_in_chunks(const char* filename) {
  File file = LittleFS.open(filename, "rb");
  if (file) {
    Serial.println("File opened successfully");
    uint8_t chunk[CHUNK_SIZE];
    size_t bytesRead;

    // Read and process the file in chunks
    while ((bytesRead = file.read(chunk, CHUNK_SIZE)) > 0) {
      //Serial.println("File content:");
      size_t chunk_Size = sizeof(chunk);
      combineArraysAndCRC( chunk, chunk_Size, resultArray);
      
      if(counter==last_packet_round)
      {
        last_packet=true;
        /*for(int i=0 ; i<= last_packet_size ; i++)
        { 
          Serial.print(resultArray[i],HEX );
          Serial.print(" ");
        }*/
        
      }else
      {
        
        /*for(int i=0 ; i<= 138 ; i++)
        { 
          Serial.print(resultArray[i] );
          Serial.print(" ");
        }*/
        
      }
      
      //Serial.println();
      counter++;

      write();
      Serial.println(counter);
      while(Serial2.read()!=0xAA);
      //delay(1250);
      /*
      for (size_t i = 0; i < bytesRead; i++) {
        Serial.print("0x");
        Serial.print(chunk[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
      */
      //Serial.println("after:");
    }
   
    address = 0x08008000;  // to return the initial address again in case there are any new updates
    file.close();
  } else {
    Serial.println("Error opening file");
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
  while(Serial2.read()!=0xAA);
}

void Jump_to_app()
{
   Serial2.write(4);
   delay(10);
}

void write()
{
  Serial2.write(resultArray[0]);
  delay(10);
  if(last_packet)
  {
    for(int i=1;i<=last_packet_size;i++)
    {
      Serial.println("I AM IN");
      Serial2.write(resultArray[i]);
      delay(10);
    }    
  }else
  {
    for(int i=1;i<=138;i++)
    {
      Serial2.write(resultArray[i]);
      delay(10);
    }

  }

  
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

 // Change this to your desired password
String storedDotp = "";
String storedVotp = "";
// Function to handle OTP reception via HTTP POST
bool otpsReceived = false ;
void handleOtp() {
  if (server.hasArg("Dotp") && server.hasArg("Votp")) {
    storedDotp = server.arg("Dotp");
    storedVotp = server.arg("Votp");
    Serial.println("Received Dotp: " + storedDotp);
    Serial.println("Received Votp: " + storedVotp);
    server.send(200, "text/plain", "OTPs received and stored");
    otpsReceived = true ;
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
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

  if (enteredPassword == storedDotp) {
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
  delay(1000); // Delay before clearing the LCD and asking for password again
  lcd.clear();
}
void readFile(const char *path) {
  Serial.print("Reading file: ");
  Serial.println(path);
  File file =LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  //Serial.print("Read from file: ");

  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("\n");
}


void setup() {
  // Initialize arrays with data

  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Welcome Car 1");
  initWiFi();
  server.on("/otp", HTTP_POST, handleOtp);
  server.begin();
  Serial2.begin(115200, SERIAL_8N1, RXp2, TXp2);
  // Do something with the result array...
  initwifi2(); 
}

int i =0,j=0; ;
void loop() {
  server.handleClient();
  //while(storedDotp=="");
j=0;
  if(otpsReceived){
    if (taskCompleted) {
  taskCompleted = false;
  if (Firebase.Storage.download(&fbdo, STORAGE_BUCKET_ID, "application.bin", "/downloaded_file.bin", mem_storage_type_flash, fcsDownloadCallback)) {
     // Firebase.Storage.download(&fbdo, STORAGE_BUCKET_ID, "application.bin", "/downloaded_file1.bin", mem_storage_type_flash, fcsDownloadCallback);
      Serial.println("File downloaded successfully");
      //decryptFile("/downloaded_file.bin", "/D_downloaded_file.bin");
      // readFile("/downloaded_file1.bin");
      // readFile("/D_downloaded_file.bin");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("New update");
      delay(1000);
      enter_pass();
      earse_App();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Updating...!");
      delay(500);
      read_binary_file_in_chunks("/downloaded_file.bin");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Finished!");
      Jump_to_app();
      delay(1000);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Welcome Car 1");
      delay(2000);
    } else {
      Serial.println("File download failed");
    }
  }
  send_v2v("/downloaded_file.bin");
  deinitWiFi() ;

  //delay(200);
  
  initwifi2();
  
  while(i<last_packet_round+1)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sending");
  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myarr[i], sizeof(myarr[i]));
      for(int j=0 ; j<= 138 ; j++)
        { 
          Serial.print(myarr[i][j] );
          Serial.print(" ");
        }
    Serial.println(" ");



  if (result == ESP_OK) {
   // Serial.println("Sending confirmed");
  }
  else {
    Serial.println("Sending error");
  }
  i++;
  delay(200);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Welcome Car 1");

  while(1);
  }
}



