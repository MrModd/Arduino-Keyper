/*  Keyper - A Pasword Keeper
 *  v2.2 build 20130405
 *
 *  Starting project date: 29/01/2013
 *
 *  Copyright (C) 2013  Federico "MrModd" Cosentino (http://mrmodd.it/)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Wire.h> //I2C library
#include <AESLib.h> //AES 128 bit encoding library

/* ===== HARDWARE ADDRESSES ===== */
#define IN_CIRCUIT_LED       13 //Arduino LED
#define EEPROM_ADDR          0x50 //1010 000 (it's a 7-bit address)
#define EEPROM_WRITE_PROTECT 4
#define RED_LED              5 //Must be a PWM output
#define GREEN_LED            6 //Must be a PWM output
#define BUTTON_OK            7
#define BUTTON_1             8
#define BUTTON_2             9
#define BUTTON_3             11
#define BUTTON_4             10

/* ========== CONSTANTS ========== */
#define MAX_ADDRESS                    32768 //Last address + 1 of the EEPROM (32768 == 256kb EEPROM)
#define KEY_LEN                        16 //Length of the key used for encrypt/decrypt passwords. This MUST be 16
#define PIN_BUFFER_LEN                 KEY_LEN //Max dimension for pin buffer (must be at least greater or equal than the dimension of pin stored on EEPROM)
#define SERIAL_BUFFER_LEN              128 //Max dimension for the serial buffer (must be at least greater or equal than the dimension of each data stored on EEPROM)
#define TIMEOUT                        100 //for 5 seconds (delay(50))
#define LOCK_TIMEOUT                   2400 //2 minutes (delay(50))
#define LONG_PRESS                     3000 //milliseconds to wait for a long press behaviour
#define PASSWORD_1                     0 // Password 1 index
#define PASSWORD_2                     1 // Password 2 index
#define PASSWORD_3                     2 // Password 3 index
#define PASSWORD_4                     3 // Password 4 index
const char challenge_mesg[] =          "MrModd"; //It is used to test if the inserted pin is the correct one (see checkPin() function)
const char wait[] =                    "Wait...";
const char done[] =                    "Done!";
const char too_long[] =                "\r\nToo long!";

/* ======== DATA ADDRESSES ======== */
//Welcome message
#define INITIAL_MESSAGE_ADDR           0
#define INITIAL_MESSAGE_LEN            100
//Encrypted message used to challenge pin typed by user. It contains a well known message, defined by the constant challenge_mesg (see above)
#define KEY_MESG_ADDR                  100
#define KEY_MESG_LEN                   16 //It MUST be multiple of 16, because the encoding library encrypt/decrypt block of 16 bytes of data
const unsigned int PASS_ADDR[] = {
  116,   // Password 1
  244,   // Password 2
  372,   // Password 3
  500 }; // Password 4
const unsigned int PASS_LEN[] = { //It MUST be multiple of 16, because the encoding library encrypt/decrypt block of 16 bytes of data
  128,   // Password 1
  128,   // Password 2
  128,   // Password 3
  128 }; // Password 4

/* ========== VARIABLES ========== */
int pwm_val; //Current PWM value for the LED
boolean growing; //True if pwm value for the LED is increasing, false if it's decreasing
boolean locked; //True if the device's pin has not been entered yet
boolean serial; //True if serial console has been opened
int timeout; //Timeout before the reset of the device
unsigned int lock_timeout; //Timeout before locking the device
uint8_t pin_buffer[PIN_BUFFER_LEN]; //It will keep temporary data from keypad
uint8_t pin_buffer_index;
char serial_buffer[SERIAL_BUFFER_LEN + 1]; //It will keep temporary data from serial console
uint8_t serial_buffer_index;
uint8_t key[KEY_LEN]; //While the device is unlocked, it keeps the key to decrypt data stored in EEPROM

/* ========== FUNCTIONS ========== */
void blinkLED(uint8_t led);
void flashLED(uint8_t led);
void errorLED();
boolean checkPin(void);
void typePassword(uint8_t n);
void serialPrintWelcomeMessage(void);
void serialPrintMenu(void);
void serialManageOption(void);
void wipeData(void);
boolean readLine(char *buffer, uint8_t length, boolean hide_typing);
void readPin(uint8_t *buffer, uint8_t length);
boolean i2c_1024kb_eeprom_read_byte(byte i2caddr, unsigned long byteaddr, byte *data);
void i2c_1024kb_eeprom_write_byte(byte i2caddr, unsigned long byteaddr, byte data);
boolean i2c_1024kb_eeprom_read_buffer(byte i2caddr, unsigned long byteaddr, byte data[], unsigned int length);
void i2c_1024kb_eeprom_write_page(byte i2caddr, unsigned long byteaddr, byte data[], unsigned int length);
boolean i2c_1024kb_eeprom_erase_bytes(byte i2caddr, unsigned long baseaddr, unsigned int length);
boolean i2c_eeprom_read_byte(byte i2caddr, unsigned int byteaddr, byte *data);
void i2c_eeprom_write_byte(byte i2caddr, unsigned int byteaddr, byte data);
boolean i2c_eeprom_read_buffer(byte i2caddr, unsigned int byteaddr, byte data[], unsigned int length);
boolean i2c_eeprom_write_page(byte i2caddr, unsigned int byteaddr, byte data[], unsigned int length);
boolean i2c_eeprom_erase_bytes(byte i2caddr, unsigned int baseaddr, unsigned int length);

void setup() 
{
  //Initialization
  pinMode(IN_CIRCUIT_LED, OUTPUT);
  for(int i = 0; i <= 255; i += 5) {
    analogWrite(IN_CIRCUIT_LED, i);
    delay(10);
  }

  //Setting I/O ports
  pinMode(EEPROM_WRITE_PROTECT, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUTTON_OK, INPUT_PULLUP);
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);

  //Enable write protect on the EEPROM
  digitalWrite(EEPROM_WRITE_PROTECT, HIGH);

  //Initialize libraries
  Serial.begin(9600); //Initialize Virtual Serial
  Keyboard.begin(); //Initialize Keyboard library
  Wire.begin(); //Initialize I2C for EEPROM

  //Initialize variables
  pwm_val = 0;
  growing = true;
  locked = true;
  serial = false;
  timeout = -1;
  lock_timeout = LOCK_TIMEOUT;
  pin_buffer_index = 0;
  serial_buffer_index = 0;
  for (uint8_t i = 0; i < PIN_BUFFER_LEN; i++) pin_buffer[i] = 0;
  for (uint8_t i = 0; i < SERIAL_BUFFER_LEN; i++) serial_buffer[i] = 0;
  for (uint8_t i = 0; i < KEY_LEN; i++) key[i] = 0;
  
  //Initialization complete
  for(int i = 255; i >= 0; i -= 5) {
    analogWrite(IN_CIRCUIT_LED, i);
    delay(10);
  }
}

void loop() 
{
  /*
   * ===============
   * Serial controls
   * ===============
   */
  if(Serial) {
    // First connection
    if (!serial && locked) {
      Serial.println("Keyper is locked!");
      serial = true;
    }
    else if (!serial && !locked) {
      Serial.println("Keyper is unlocked!");
      serialPrintWelcomeMessage();
      serialPrintMenu();
      while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
      serial = true;
    }
    
    while (Serial.available()) {
      serial_buffer[serial_buffer_index++] = (char)Serial.read(); //Read next byte
      Serial.print(serial_buffer[serial_buffer_index - 1]); //Print what you read
      
      //Managing input
      //If a backspace character was read
      if (serial_buffer[serial_buffer_index - 1] == '\b') {
        serial_buffer[--serial_buffer_index] = 0; //Remove the backspace character
        Serial.print(" \b");
        if (serial_buffer_index > 0) { 
          serial_buffer[--serial_buffer_index] = 0; //Remove the deleted character
        }
      }
      //If an endline character was read
      else if (serial_buffer[serial_buffer_index - 1] == '\n' || serial_buffer[serial_buffer_index - 1] == '\r') {
        if (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
          Serial.read(); //In case serial console uses /r/n as endline command, delete the second character too
        }
        Serial.println("");
        serial_buffer[--serial_buffer_index] = 0;
        if (serial_buffer_index > 0) {
          if (locked) {
            if (strncmp(serial_buffer, "wipe", SERIAL_BUFFER_LEN) == 0) {
              digitalWrite(RED_LED, LOW);
              
              wipeData();
              
              analogWrite(RED_LED, pwm_val);
            }
            else {
              Serial.println("Type 'wipe' to erase all memory.\r\nFor all other options you must unlock the device first.");
            }
          }
          else {
            serialManageOption();
          }
        }
        while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
        while(Serial.available()) Serial.read(); //Empty incoming queue
      }
      //If there are too much data in serial input (more than buffer dimension)
      else if (serial_buffer_index >= SERIAL_BUFFER_LEN) {
        Serial.println(too_long);
        while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
        while(Serial.available()) Serial.read(); //Empty incoming queue
      }
    }
  }
  else if (serial) { //Should never happen
    serial = false;
  }

  /*
   * ============================
   * Growing and timeouts controls
   * ============================
   */
  if (timeout == -1) {
    if (locked) {
      analogWrite(RED_LED, pwm_val);
    }
    else {
      analogWrite(GREEN_LED, pwm_val);
    }
    if (growing) {
      if (pwm_val < 255) {
        pwm_val+=5;
      }
      else {
        growing = !growing;
        pwm_val-=5;
      }
    }
    else {
      if (pwm_val > 0) {
        pwm_val-=5;
      }
      else {
        growing = !growing;
        pwm_val+=5;
      }
    }
  }
  else { //Decrease digit timeout
    timeout--;
    if (timeout < 0) {
      //Reset buffer
      while(pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0;
    }
  }
  //Decrease device timeout
  if (lock_timeout > 0 && !locked) {
    lock_timeout--;
  }
  else if (lock_timeout == 0 && !locked) { //After LOCK_TIMEOUT*50 ms lock the device if unlocked
    //Lock the device
    digitalWrite(GREEN_LED, LOW);
    timeout = -1;
    while(pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0;
    for (uint8_t i = 0; i < KEY_LEN; i++) key[i] = 0;
    locked = true;
    if (serial) {
      Serial.println("Keyper is now locked!");
    }
    analogWrite(RED_LED, pwm_val);
  }

  /*
   * ================
   * Buttons controls
   * ================
   */
  if (digitalRead(BUTTON_OK) == LOW) {
    unsigned long time = millis();
    while(digitalRead(BUTTON_OK) == LOW && millis() - time < LONG_PRESS); //Wait until key is released or the keystroke is considered a long pressure
    
    if (millis() - time >= LONG_PRESS) { //If it was a long pressure
      //Lock the device
      digitalWrite(GREEN_LED, LOW);
      timeout = -1;
      while(pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0;
      for (uint8_t i = 0; i < KEY_LEN; i++) key[i] = 0;
      locked = true;
      if (serial) {
        Serial.println("Keyper is now locked!");
      }
      analogWrite(RED_LED, pwm_val);
      while(digitalRead(BUTTON_OK) == LOW); //Wait until key is released
    }
    else {
      //Keypad is locked
      if (locked) {
        digitalWrite(RED_LED, LOW);
        //Check if the pin inserted is correct
        if (checkPin()) { //Pin is correct
          //Notify to the user
          blinkLED(GREEN_LED);
          //Unlock the device
          locked = false;
          //Reset timeout
          timeout = -1;
          lock_timeout = LOCK_TIMEOUT;
          //Empty buffer
          while (pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0;
          
          //Print on serial
          if (serial) {
            Serial.println("\r\nKeyper is now unlocked!");
            serialPrintWelcomeMessage();
            serialPrintMenu();
            while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
            while(Serial.available()) Serial.read(); //Empty incoming queue
          }
          
          //Turn on green LED
          analogWrite(GREEN_LED, pwm_val);
        }
        else { //Pin is not correct
          //Notify to the user
          blinkLED(RED_LED);
          
          //Print on serial
          if (serial) {
            Serial.println("Wrong pin!");
          }
          
          //Restore red LED
          analogWrite(RED_LED, pwm_val);
        }
      }
      //Keypad is not locked
      else { //If (!locked)
        if (pin_buffer[0] == 0) { //No password was selected
          //Notify to the user
          digitalWrite(GREEN_LED, LOW);
          blinkLED(RED_LED);
          
          //Print on serial
          if (serial) {
            Serial.println("Please, select a password first.");
          }
          
          //Restore green LED
          analogWrite(GREEN_LED, pwm_val);
        }
        else {
          //Notify to the user
          blinkLED(GREEN_LED);
          
          //Write password
          switch (pin_buffer[0]) {
          case 1:
            typePassword(PASSWORD_1);
            if (serial) {
              Serial.println("Password 1 written!");
            }
            break;
          case 2:
            typePassword(PASSWORD_2);
            if (serial) {
              Serial.println("Password 2 written!");
            }
            break;
          case 3:
            typePassword(PASSWORD_3);
            if (serial) {
              Serial.println("Password 3 written!");
            }
            break;
          case 4:
            typePassword(PASSWORD_4);
            if (serial) {
              Serial.println("Password 4 written!");
            }
            break;
          }
          
          //Restore green LED
          analogWrite(GREEN_LED, pwm_val);
        }
        //Reset lock timeout
        lock_timeout = LOCK_TIMEOUT;
      }
      //Reset buffer
      while(pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0;
      //Reset timeout
      timeout = -1;
    }
  } /* if (digitalRead(BUTTON_OK) == LOW) */
  else if (digitalRead(BUTTON_1) == LOW) {
    while(digitalRead(BUTTON_1) == LOW); //Wait until key is released
    //If locked
    if (locked && pin_buffer_index < PIN_BUFFER_LEN) {
      //Notify to the user
      digitalWrite(RED_LED, LOW);
      flashLED(GREEN_LED);
      //Add value to the buffer
      pin_buffer[pin_buffer_index++] = 1;
      //Start timeout
      timeout = TIMEOUT;
    }
    //If not locked
    else if (!locked) {
      //Notify to the user
      digitalWrite(GREEN_LED, LOW);
      flashLED(GREEN_LED);
      //Add value to the buffer
      pin_buffer[0] = 1;
      pin_buffer_index = 1;
      //Start timeout
      timeout = TIMEOUT;
      //Reset lock timeout
      lock_timeout = LOCK_TIMEOUT;
    }
  }
  else if (digitalRead(BUTTON_2) == LOW) {
    while(digitalRead(BUTTON_2) == LOW); //Wait until key is released
    //If locked
    if (locked && pin_buffer_index < PIN_BUFFER_LEN) {
      //Notify to the user
      digitalWrite(RED_LED, LOW);
      flashLED(GREEN_LED);
      //Add value to the buffer
      pin_buffer[pin_buffer_index++] = 2;
      //Start timeout
      timeout = TIMEOUT;
    }
    //If not locked
    else if (!locked) {
      //Notify to the user
      digitalWrite(GREEN_LED, LOW);
      flashLED(GREEN_LED);
      //Add value to the buffer
      pin_buffer[0] = 2;
      pin_buffer_index = 1;
      //Start timeout
      timeout = TIMEOUT;
      //Reset lock timeout
      lock_timeout = LOCK_TIMEOUT;
    }
  }
  else if (digitalRead(BUTTON_3) == LOW) {
    while(digitalRead(BUTTON_3) == LOW); //Wait until key is released
    if (locked && pin_buffer_index < PIN_BUFFER_LEN) {
      //If locked
      //Notify to the user
      digitalWrite(RED_LED, LOW);
      flashLED(GREEN_LED);
      //Add value to the buffer
      pin_buffer[pin_buffer_index++] = 3;
      //Start timeout
      timeout = TIMEOUT;
    }
    //If not locked
    else if (!locked) {
      //Notify to the user
      digitalWrite(GREEN_LED, LOW);
      flashLED(GREEN_LED);
      //Add value to the buffer
      pin_buffer[0] = 3;
      pin_buffer_index = 1;
      //Start timeout
      timeout = TIMEOUT;
      //Reset lock timeout
      lock_timeout = LOCK_TIMEOUT;
    }
  }
  else if (digitalRead(BUTTON_4) == LOW) {
    while(digitalRead(BUTTON_4) == LOW); //Wait until key is released
    //If locked
    if (locked && pin_buffer_index < PIN_BUFFER_LEN) {
      //Notify to the user
      digitalWrite(RED_LED, LOW);
      flashLED(GREEN_LED);
      //Add value on the buffer
      pin_buffer[pin_buffer_index++] = 4;
      //Start timeout
      timeout = TIMEOUT;
    }
    //If not locked
    else if (!locked) {
      //Notify to the user
      digitalWrite(GREEN_LED, LOW);
      flashLED(GREEN_LED);
      //Add value on the buffer
      pin_buffer[0] = 4;
      pin_buffer_index = 1;
      //Start timeout
      timeout = TIMEOUT;
      //Reset lock timeout
      lock_timeout = LOCK_TIMEOUT;
    }
  }
  
  delay(50);
}

//Used to notify a successful or a failure operation
void blinkLED(uint8_t led)
{
  digitalWrite(led, HIGH);
  delay(100);
  digitalWrite(led, LOW);
  delay(100);
  digitalWrite(led, HIGH);
  delay(100);
  digitalWrite(led, LOW);
  delay(100);
  digitalWrite(led, HIGH);
  delay(100);
  digitalWrite(led, LOW);
}

//Used to notify a keystroke on the keypad
void flashLED(uint8_t led)
{
  digitalWrite(led, HIGH);
  delay(100);
  digitalWrite(led, LOW);
}

//Used to notify an error event (eg. cannot read the EEPROM)
void errorLED(void)
{
  digitalWrite(RED_LED, HIGH);
  delay(1000);
  digitalWrite(RED_LED, LOW);
  delay(200);
  digitalWrite(RED_LED, HIGH);
  delay(1000);
  digitalWrite(RED_LED, LOW);
  delay(1000);
}

/* Argument is the password received by buttons
 * Returns true if the password is correct, false otherwise
 */
boolean checkPin(void)
{
  char message[KEY_MESG_LEN];
  //Reading message from EEPROM
  if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, KEY_MESG_ADDR, (byte*)message, KEY_MESG_LEN)) {
    /*if (serial) {
      Serial.println("Error while reading EEPROM");
    }*/
    errorLED();
    return false;
  }
  
  //Decrypting
  for (uint8_t i = 0; i < KEY_MESG_LEN; i+=16) {
    aes128_dec_single(pin_buffer, message+i);
  }
  
  if (strncmp(message, challenge_mesg, KEY_MESG_LEN) == 0) {
    //If the decrypted message is equal to the challenge message, then the pin inserted is correct
    for (uint8_t i = 0; i < KEY_LEN; i++) {
      key[i] = pin_buffer[i];
    }
    return true;
  }
  else {
    return false;
  }
}

/* Argument is one of the constants PASSWORD_x (where x is the number of the passowrd) */
void typePassword(uint8_t n)
{
  //Variables declaration
  char *pass = (char *)malloc(sizeof(char)*PASS_LEN[n]);
  if (pass == NULL) {
    /*if (serial) {
      Serial.println("Error in malloc().");
    }*/
    errorLED();
    return;
  }

  //Reading
  if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, PASS_ADDR[n], (byte *)pass, PASS_LEN[n])) {
    /*if (serial) {
      Serial.println("Error while reading EEPROM");
    }*/
    errorLED();
    free(pass);
    return;
  }
  
  //Decrypting
  for (uint8_t i = 0; i < PASS_LEN[n]; i+=16) {
    aes128_dec_single(key, pass+i);
  }

  //Writing
  for (uint8_t i = 0; i < PASS_LEN[n] && pass[i] != '\0'; i++) {
    Keyboard.print(pass[i]);
  }

  free(pass);
}

void serialPrintWelcomeMessage(void)
{
  //Variable declaration
  char mess[INITIAL_MESSAGE_LEN];
  
  //Reading
  if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, INITIAL_MESSAGE_ADDR, (byte *)mess, INITIAL_MESSAGE_LEN)) {
    //Serial.println("Error reading welcome message.");
    return;
  }

  //Writing
  Serial.print("Welcome message: ");
  for (uint8_t i = 0; i < INITIAL_MESSAGE_LEN && mess[i] != '\0'; i++) {
    Serial.print(mess[i]);
  }
  Serial.println("");
}

void serialPrintMenu(void)
{
  Serial.println("\r\nKeyper by MrModd v 2.2 build 20130405\r\nA Password keeper and typer\r\n");
  Serial.println("Make your choice:");
  Serial.println("help ........ Print this help");
  Serial.println("lock ........ Lock the device");
  Serial.println("wipe ........ Wipe all memory");
  Serial.println("welcome ..... Overwrite initial message");
  Serial.println("pin ......... Overwrite device pin");
  Serial.println("p1 .......... Overwrite password 1");
  Serial.println("p2 .......... Overwrite password 2");
  Serial.println("p3 .......... Overwrite password 3");
  Serial.println("p4 .......... Overwrite password 4");
  Serial.println("");
}

void serialManageOption()
{
  while(Serial.available()) Serial.read(); //Empty incoming queue
  
  //Test options
  if (strncmp(serial_buffer, "help", SERIAL_BUFFER_LEN + 1) == 0) {
    //Print help menu
    serialPrintMenu();
  }
  else if (strncmp(serial_buffer, "lock", SERIAL_BUFFER_LEN + 1) == 0) {
    //Lock the device
    digitalWrite(GREEN_LED, LOW);
    timeout = -1;
    while(pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0;
    for (uint8_t i = 0; i < KEY_LEN; i++) key[i] = 0;
    locked = true;
    Serial.println("Keyper is now locked!");
    analogWrite(RED_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "wipe", SERIAL_BUFFER_LEN + 1) == 0) {
    //Clear data stored in EEPROM
    digitalWrite(GREEN_LED, LOW);
    
    wipeData();
    
    analogWrite(GREEN_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "welcome", SERIAL_BUFFER_LEN + 1) == 0) {
    //Overwrite welcome message
    digitalWrite(GREEN_LED, LOW);
    Serial.print("Now type the welcome message (max ");
    Serial.print(INITIAL_MESSAGE_LEN);
    Serial.println(" characters):");
    
    while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
    
    if (readLine(serial_buffer, INITIAL_MESSAGE_LEN + 1, false)) { //+1 because \n or \r is counted with other characters, but it is set to \0 when returning from readLine() function
      Serial.println(wait);
      digitalWrite(EEPROM_WRITE_PROTECT, LOW);
      delay(5);
      
      //Write message
      //Serial.println("Writing bytes...");
      i2c_1024kb_eeprom_write_page(EEPROM_ADDR, INITIAL_MESSAGE_ADDR, (byte *)serial_buffer, INITIAL_MESSAGE_LEN);
      
      digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
      Serial.println(done);
    }
    else {
      Serial.println(too_long);
    }
    
    analogWrite(GREEN_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "pin", SERIAL_BUFFER_LEN + 1) == 0) {
    //Overwrite pin
    char *mesg;
    
    Serial.print("Now type the pin code on the device (max ");
    Serial.print(KEY_LEN);
    Serial.println(" digits):");
    
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, HIGH);
    timeout = -1;
    while (pin_buffer_index > 0) pin_buffer[--pin_buffer_index] = 0; //Empty buffer
    
    readPin(pin_buffer, KEY_LEN);
    
    Serial.println(wait);
    digitalWrite(EEPROM_WRITE_PROTECT, LOW);
    delay(5);
    
    
    //Serial.println("Writing encoded challenge message for new pin...");
    mesg = (char *)malloc(sizeof(char)*KEY_MESG_LEN);
    if (mesg == NULL) {
      //Serial.println("Error in malloc().");
      return;
    }
    for (uint8_t i = 0; i < KEY_MESG_LEN; i++) {
      mesg[i] = 0;
    }
    strncpy(mesg, challenge_mesg, KEY_MESG_LEN);
    for (uint8_t i = 0; i < KEY_MESG_LEN; i+=16) {
      aes128_enc_single(pin_buffer, mesg+i);
    }
    i2c_1024kb_eeprom_write_page(EEPROM_ADDR, KEY_MESG_ADDR, (byte *)mesg, KEY_MESG_LEN);
    free(mesg);
    
    //Serial.println("Rewriting password 1...");
    mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_1]);
    if (mesg == NULL) {
      //Serial.println("Error in malloc().");
      return;
    }
    if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, PASS_ADDR[PASSWORD_1], (byte *)mesg, PASS_LEN[PASSWORD_1])) {
      //Serial.println("Error while reading EEPROM");
      return;
    }
    for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i+=16) {
      aes128_dec_single(key, mesg+i);
      aes128_enc_single(pin_buffer, mesg+i);
    }
    //Writing
    i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_1], (byte *)mesg, PASS_LEN[PASSWORD_1]);
    free(mesg);
    
    //Serial.println("Rewriting password 2...");
    mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_2]);
    if (mesg == NULL) {
      //Serial.println("Error in malloc().");
      return;
    }
    if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, PASS_ADDR[PASSWORD_2], (byte *)mesg, PASS_LEN[PASSWORD_2])) {
      //Serial.println("Error while reading EEPROM");
      return;
    }
    for (uint8_t i = 0; i < PASS_LEN[PASSWORD_2]; i+=16) {
      aes128_dec_single(key, mesg+i);
      aes128_enc_single(pin_buffer, mesg+i);
    }
    //Writing
    i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_2], (byte *)mesg, PASS_LEN[PASSWORD_2]);
    free(mesg);
    
    //Serial.println("Rewriting password 3...");
    mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_3]);
    if (mesg == NULL) {
      //Serial.println("Error in malloc().");
      return;
    }
    if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, PASS_ADDR[PASSWORD_3], (byte *)mesg, PASS_LEN[PASSWORD_3])) {
      //Serial.println("Error while reading EEPROM");
      return;
    }
    for (uint8_t i = 0; i < PASS_LEN[PASSWORD_3]; i+=16) {
      aes128_dec_single(key, mesg+i);
      aes128_enc_single(pin_buffer, mesg+i);
    }
    //Writing
    i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_3], (byte *)mesg, PASS_LEN[PASSWORD_3]);
    free(mesg);
    
    //Serial.println("Rewriting password 4...");
    mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_4]);
    if (mesg == NULL) {
      //Serial.println("Error in malloc().");
      return;
    }
    if (!i2c_1024kb_eeprom_read_buffer(EEPROM_ADDR, PASS_ADDR[PASSWORD_4], (byte *)mesg, PASS_LEN[PASSWORD_4])) {
      //Serial.println("Error while reading EEPROM");
      return;
    }
    for (uint8_t i = 0; i < PASS_LEN[PASSWORD_4]; i+=16) {
      aes128_dec_single(key, mesg+i);
      aes128_enc_single(pin_buffer, mesg+i);
    }
    //Writing
    i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_4], (byte *)mesg, PASS_LEN[PASSWORD_4]);
    free(mesg);
    
    for (uint8_t i = 0; i < KEY_LEN; i++) {
      key[i] = pin_buffer[i];
    }
    
    digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
    Serial.println(done);
    delay(100); //Without this, returning in loop() function, BUTTON_OK will be seen as pressed
    
    digitalWrite(RED_LED, LOW);
    analogWrite(GREEN_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "p1", SERIAL_BUFFER_LEN + 1) == 0) {
    //Overwrite password
    digitalWrite(GREEN_LED, LOW);
    Serial.print("Now type the first password (max ");
    Serial.print(PASS_LEN[PASSWORD_1]);
    Serial.println(" characters):");
    
    while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
    
    if (readLine(serial_buffer, PASS_LEN[PASSWORD_1] + 1, true)) { //+1 because \n or \r is counted with other characters, but it is set to \0 when returning from readLine() function
      Serial.println(wait);
      digitalWrite(EEPROM_WRITE_PROTECT, LOW);
      delay(5);
      
      //Encripting
      //Serial.println("Encripting bytes...");
      for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i+=16) {
        aes128_enc_single(key, serial_buffer+i);
      }
      
      //Write password
      //Serial.println("Writing bytes...");
      i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_1], (byte *)serial_buffer, PASS_LEN[PASSWORD_1]);
      digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
      Serial.println(done);
    }
    else {
      Serial.println(too_long);
    }
    analogWrite(GREEN_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "p2", SERIAL_BUFFER_LEN + 1) == 0) {
    //Overwrite password
    digitalWrite(GREEN_LED, LOW);
    Serial.print("Now type the second password (max ");
    Serial.print(PASS_LEN[PASSWORD_2]);
    Serial.println(" characters):");
    
    while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
    
    if (readLine(serial_buffer, PASS_LEN[PASSWORD_2] + 1, true)) { //+1 because \n or \r is counted with other characters, but it is set to \0 when returning from readLine() function
      Serial.println(wait);
      digitalWrite(EEPROM_WRITE_PROTECT, LOW);
      delay(5);
      
      //Encripting
      //Serial.println("Encripting bytes...");
      for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i+=16) {
        aes128_enc_single(key, serial_buffer+i);
      }
      
      //Write password
      //Serial.println("Writing bytes...");
      i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_2], (byte *)serial_buffer, PASS_LEN[PASSWORD_2]);
      digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
      Serial.println(done);
    }
    else {
      Serial.println(too_long);
    }
    analogWrite(GREEN_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "p3", SERIAL_BUFFER_LEN + 1) == 0) {
    //Overwrite password
    digitalWrite(GREEN_LED, LOW);
    Serial.print("Now type the third password (max ");
    Serial.print(PASS_LEN[PASSWORD_3]);
    Serial.println(" characters):");
    
    while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
    
    if (readLine(serial_buffer, PASS_LEN[PASSWORD_3] + 1, true)) { //+1 because \n or \r is counted with other characters, but it is set to \0 when returning from readLine() function
      Serial.println(wait);
      digitalWrite(EEPROM_WRITE_PROTECT, LOW);
      delay(5);
      
      //Encripting
      //Serial.println("Encripting bytes...");
      for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i+=16) {
        aes128_enc_single(key, serial_buffer+i);
      }
      
      //Write password
      //Serial.println("Writing bytes...");
      i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_3], (byte *)serial_buffer, PASS_LEN[PASSWORD_3]);
      digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
      Serial.println(done);
    }
    else {
      Serial.println(too_long);
    }
    analogWrite(GREEN_LED, pwm_val);
  }
  else if (strncmp(serial_buffer, "p4", SERIAL_BUFFER_LEN + 1) == 0) {
    //Overwrite password
    digitalWrite(GREEN_LED, LOW);
    Serial.print("Now type the forth password (max ");
    Serial.print(PASS_LEN[PASSWORD_4]);
    Serial.println(" characters):");
    
    while (serial_buffer_index > 0) serial_buffer[--serial_buffer_index] = 0; //Empty buffer
    
    if (readLine(serial_buffer, PASS_LEN[PASSWORD_4] + 1, true)) { //+1 because \n or \r is counted with other characters, but it is set to \0 when returning from readLine() function
      Serial.println(wait);
      digitalWrite(EEPROM_WRITE_PROTECT, LOW);
      delay(5);
      
      //Encripting
      //Serial.println("Encripting bytes...");
      for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i+=16) {
        aes128_enc_single(key, serial_buffer+i);
      }
      
      //Write password
      //Serial.println("Writing bytes...");
      i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_4], (byte *)serial_buffer, PASS_LEN[PASSWORD_4]);
      digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
      Serial.println(done);
    }
    else {
      Serial.println(too_long);
    }
    analogWrite(GREEN_LED, pwm_val);
  }
  else {
    Serial.println("Invalid option.");
  }
}

void wipeData(void)
{
  uint8_t empty_key[KEY_LEN];
  char *mesg;
  
  for (uint8_t i = 0; i < KEY_LEN; i++) {
    empty_key[i] = 0;
  }
  
  Serial.println(wait);
  digitalWrite(EEPROM_WRITE_PROTECT, LOW);
  delay(5);
  
  //Serial.println("Erasing welcome message...");
  if (!i2c_1024kb_eeprom_erase_bytes(EEPROM_ADDR, INITIAL_MESSAGE_ADDR, INITIAL_MESSAGE_LEN)) {
    //Serial.println("Error erasing memory.");
    return;
  }
  
  //Serial.println("Writing encoded challenge message for empty pin");
  mesg = (char *)malloc(sizeof(char)*KEY_MESG_LEN);
  if (mesg == NULL) {
    //Serial.println("Error in malloc().");
    return;
  }
  for (uint8_t i = 0; i < KEY_MESG_LEN; i++) {
    mesg[i] = 0;
  }
  //Copy challenge message to mesg
  strncpy(mesg, challenge_mesg, KEY_MESG_LEN);
  //Crypt challenge message
  for (uint8_t i = 0; i < KEY_MESG_LEN; i+=16) {
    aes128_enc_single(empty_key, mesg+i);
  }
  //Write encrypted challenge message on EEPROM
  i2c_1024kb_eeprom_write_page(EEPROM_ADDR, KEY_MESG_ADDR, (byte *)mesg, KEY_MESG_LEN);
  free(mesg);
  
  //Write an encrypted version of an empty password of length PASS_LEN[PASSWORD_1]
  //Serial.println("Erasing password 1...");
  mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_1]);
  if (mesg == NULL) {
    //Serial.println("Error in malloc().");
    return;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i++) {
    mesg[i] = 0;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_1]; i+=16) {
    aes128_enc_single(empty_key, mesg+i);
  }
  i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_1], (byte*)mesg, PASS_LEN[PASSWORD_1]);
  free(mesg);
  
  //Write an encrypted version of an empty password of length PASS_LEN[PASSWORD_2]
  //Serial.println("Erasing password 2...");
  mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_2]);
  if (mesg == NULL) {
    //Serial.println("Error in malloc().");
    return;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_2]; i++) {
    mesg[i] = 0;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_2]; i+=16) {
    aes128_enc_single(empty_key, mesg+i);
  }
  i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_2], (byte*)mesg, PASS_LEN[PASSWORD_2]);
  free(mesg);
  
  //Write an encrypted version of an empty password of length PASS_LEN[PASSWORD_3]
  //Serial.println("Erasing password 3...");
  mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_3]);
  if (mesg == NULL) {
    //Serial.println("Error in malloc().");
    return;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_3]; i++) {
    mesg[i] = 0;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_3]; i+=16) {
    aes128_enc_single(empty_key, mesg+i);
  }
  i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_3], (byte*)mesg, PASS_LEN[PASSWORD_3]);
  free(mesg);
  
  //Write an encrypted version of an empty password of length PASS_LEN[PASSWORD_4]
  //Serial.println("Erasing password 4...");
  mesg = (char *)malloc(sizeof(char)*PASS_LEN[PASSWORD_4]);
  if (mesg == NULL) {
    //Serial.println("Error in malloc().");
    return;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_4]; i++) {
    mesg[i] = 0;
  }
  for (uint8_t i = 0; i < PASS_LEN[PASSWORD_4]; i+=16) {
    aes128_enc_single(empty_key, mesg+i);
  }
  i2c_1024kb_eeprom_write_page(EEPROM_ADDR, PASS_ADDR[PASSWORD_4], (byte*)mesg, PASS_LEN[PASSWORD_4]);
  free(mesg);
  
  digitalWrite(EEPROM_WRITE_PROTECT, HIGH);
  Serial.println(done);
}

boolean readLine(char *buffer, uint8_t length, boolean hide_typing)
{
  uint8_t i = 0;
  
  while (1) {
    if (Serial.available()) {
      buffer[i++] = (char)Serial.read(); //Read next byte
      if (hide_typing) {
        Serial.print("*"); //Show a * if it is part of a password
      }
      else {
        Serial.print(buffer[i - 1]); //Show the character if it is part of a message
      }
      
      //Managing input
      //If a backspace character was read
      if (buffer[i - 1] == '\b') {
        buffer[--i] = 0; //Remove the backspace character
        if (hide_typing) {
          Serial.print("\b \b\b \b"); //Delete the * character associated to the one deleted and the * printed when \b was pressed
        }
        else {
          Serial.print(" \b"); //Delete the * character associated to the one deleted
        }
        if (i > 0) { 
          buffer[--i] = 0; //Remove the deleted character
        }
      }
      //If an endline character was read
      else if (buffer[i - 1] == '\n' || buffer[i - 1] == '\r') {
        if (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
          Serial.read(); //In case serial console uses /r/n as endline command, delete the second character too
        }
        if (hide_typing) {
          Serial.println("\b \b"); //Delete the * character written when \n or \r was received
        }
        else {
          Serial.println("");
        }
        buffer[--i] = 0; //Delete the \n or \r character
        
        return true;
      }
      else if (i >= length) { //Too much data
        //With this condition length - 1 characters can be received followed by \n or \r for a total of length
        //characters read
        return false;
      }
    }
  }
  return false; //Unreachable
}

void readPin(uint8_t *buffer, uint8_t length)
{
  uint8_t i = 0;
  int timeout = -1;
  
  while (1) {
    //Timeout
    if (timeout != -1) {
      timeout--;
      if (timeout < 0) {
        //Reset buffer and clear serial console
        while(i > 0) {
          buffer[--i] = 0;
          Serial.print("\b \b");
        }
        digitalWrite(GREEN_LED, HIGH);
        digitalWrite(RED_LED, HIGH);
      }
    }
    
    //Buttons
    if (digitalRead(BUTTON_OK) == LOW) {
      while(digitalRead(BUTTON_OK) == LOW);
      Serial.println("");
      
      //EXIT_SUCCESS
      return;
    }
    else if (digitalRead(BUTTON_1) == LOW && i < length) {
      while(digitalRead(BUTTON_1) == LOW); //Wait until key is released
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, LOW);
      
      flashLED(GREEN_LED);
      //Add value on the buffer
      buffer[i++] = 1;
      //Print a character on the serial console
      Serial.print("*");
      //Start timeout
      timeout = TIMEOUT;
    }
    else if (digitalRead(BUTTON_2) == LOW && i < length) {
      while(digitalRead(BUTTON_2) == LOW); //Wait until key is released
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, LOW);
      
      flashLED(GREEN_LED);
      //Add value on the buffer
      buffer[i++] = 2;
      //Print a character on the serial console
      Serial.print("*");
      //Start timeout
      timeout = TIMEOUT;
    }
    else if (digitalRead(BUTTON_3) == LOW && i < length) {
      while(digitalRead(BUTTON_3) == LOW); //Wait until key is released
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, LOW);
      //Print a character on the serial console
      flashLED(GREEN_LED);
      //Add value on the buffer
      buffer[i++] = 3;
      //Print a character on the serial console
      Serial.print("*");
      //Start timeout
      timeout = TIMEOUT;
    }
    else if (digitalRead(BUTTON_4) == LOW && i < length) {
      while(digitalRead(BUTTON_4) == LOW); //Wait until key is released
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, LOW);
      
      flashLED(GREEN_LED);
      //Add value on the buffer
      buffer[i++] = 4;
      //Print a character on the serial console
      Serial.print("*");
      //Start timeout
      timeout = TIMEOUT;
    }
    delay(50);
  }
}

/********************************************************************\
 *                                                                    *
 *                      Functions for accessing                       *
 *                        Microchip I2C EEPROM                        *
 *                    By Federico (MrModd) Cosentino                  *
 *                                                                    *
 *     Suitable for: 24xx64, 24xx64F, 24xx65, 24xx32A, 24xx32AF,      *
 *                   24xx128, 24xx256, 24xx512, 24xx1026              *
 *     Not applicable for: 24xx00, 24xx01, 24xx02, 24xx04, 24xx08,    *
 *                         24xx16, 24xx515, 24xx1025 and all their    *
 *                         derivatives                                *
 \********************************************************************/

/*
 * For EEPROM series 24xx1026 you must set the most significant bit of the byte address directly on the device address
 * (read the datasheet for more info)
 */
boolean i2c_1024kb_eeprom_read_byte(byte i2caddr, unsigned long byteaddr, byte *data) {
  /*
   * i2caddr should be 1010 ABC0
   * where A and B are the chip select bits, and C must be 0 if you want to access
   * the first 512kbit of the memory, and 1 for the other half part.
   * So, byteaddr is a 32bit integer because his 16th least significant bit represent
   * the block to access:
   *   eg. 0000 0000 0000 0000 1111 1111 1111 1111 represent the address of the last byte of the first block of memory
   *                         ^ (Block select bit)
   *       0000 0000 0000 0001 0000 0000 0000 0000 represent the address of the first byte of the second block of memory
   *                         ^ (Block select bit)
   * 15 most significant bits of the byte address should be always 0. That means that byteaddr should be < 131072.
   */
  i2caddr &= 0xFC; //Obtain the device address without (eventually) block-select and read-write bits
  //i2caddr example: 1010 0000 (if A2 and A1 are set to 0) or 1010 1100 (if A2 and A1 are set to 1)
  i2caddr |= (byteaddr >> 15) & 2; //Translate the block select bit from the 16th bit of the byte address to the 2nd bit of the device address and exclude all other bits (&2)
  byteaddr &= 0x0000FFFF; //Cut the address excluding the first bit of the third byte (which selects the block of memory)
  return i2c_eeprom_read_byte(i2caddr, byteaddr, data);
}

/*
 * For EEPROM series 24xx1026 you must set the most significant bit of the byte address directly on the device address
 * (read the datasheet for more info)
 */
void i2c_1024kb_eeprom_write_byte(byte i2caddr, unsigned long byteaddr, byte data) {
  /*
   * i2caddr should be 1010 ABC0
   * where A and B are the chip select bits, and C must be 0 if you want to access
   * the first 512kbit of the memory, and 1 for the other half part.
   * So, byteaddr is a 32bit integer because his 16th least significant bit represent
   * the block to access:
   *   eg. 0000 0000 0000 0000 1111 1111 1111 1111 represent the address of the last byte of the first block of memory
   *                         ^ (Block select bit)
   *       0000 0000 0000 0001 0000 0000 0000 0000 represent the address of the first byte of the second block of memory
   *                         ^ (Block select bit)
   * 15 most significant bits of the byte address should be always 0. That means that byteaddr should be < 131072.
   */
  i2caddr &= 0xFC; //Obtain the device address without (eventually) block-select and read-write bits
  //i2caddr example: 1010 0000 (if A2 and A1 are set to 0) or 1010 1100 (if A2 and A1 are set to 1)
  i2caddr |= (byteaddr >> 15) & 2; //Translate the block select bit from the 16th bit of the byte address to the 2nd bit of the device address and exclude all other bits (&2)
  byteaddr &= 0x0000FFFF; //Cut the address excluding the first bit of the third byte (which selects the block of memory)
  i2c_eeprom_write_byte(i2caddr, byteaddr, data);
}

/*
 * For EEPROM series 24xx1026 you must set the most significant bit of the byte address directly on the device address
 * (read the datasheet for more info)
 */
boolean i2c_1024kb_eeprom_read_buffer(byte i2caddr, unsigned long byteaddr, byte data[], unsigned int length) {
  boolean correct = true;
  for (unsigned int i = 0; i < length && correct; i++) {
    correct = i2c_1024kb_eeprom_read_byte(i2caddr, byteaddr+i, data+i);
    delay(5);
  }

  return correct;
}

/*
 * For EEPROM series 24xx1026 you must set the most significant bit of the byte address directly on the device address
 * (read the datasheet for more info)
 */
void i2c_1024kb_eeprom_write_page(byte i2caddr, unsigned long byteaddr, byte data[], unsigned int length) {
  for (unsigned int i = 0; i < length; i++) {
    i2c_1024kb_eeprom_write_byte(i2caddr, byteaddr+i, data[i]);
    delay(5);
  }
}

/*
 * For EEPROM series 24xx1026 you must set the most significant bit of the byte address directly on the device address
 * (read the datasheet for more info)
 */
boolean i2c_1024kb_eeprom_erase_bytes(byte i2caddr, unsigned long baseaddr, unsigned int length) {
  unsigned int i;
  boolean correct = true;
  unsigned int device, addr;
  for (i = 0; i < length && correct; i++) {
    device = i2caddr & 0xFC; //Obtain the device address without (eventually) block-select and read-write bits
    //device example: 1010 0000 (if A2 and A1 are set to 0) or 1010 1100 (if A2 and A1 are set to 1)
    device |= ((baseaddr + i) >> 15) & 2; //Translate the block select bit from the 16th bit of the byte address to the 2nd bit of the device address and exclude all other bits (&2)
    addr = (baseaddr + i) & 0x0000FFFF; //Cut the address excluding the first bit of the third byte (which selects the block of memory)
    correct = i2c_eeprom_erase_bytes(device, addr, 1);
  }

  return correct;
}

//You should wait at least 5ms between two consequential function calls
boolean i2c_eeprom_read_byte(byte i2caddr, unsigned int byteaddr, byte *data) {
  digitalWrite(IN_CIRCUIT_LED, HIGH);

  *data = 0;
  int timeout = 200; //2 seconds
  Wire.beginTransmission((int)i2caddr);
  /*
   * byteaddr is something like this: 0010 1100 0000 0111
   * first it will be sent the first byte (in the eg. 0010 1100)
   * by shifting right by 8 positions,
   * then it will be sent the rest of the address (0000 0111)
   * deleting the high-byte
   */
  byte high = (byte)((byteaddr >> 8) & 0x00FF);
  byte low = (byte)(byteaddr & 0x00FF);
  Wire.write(high); //Most significant Byte
  Wire.write(low); //Least significant Byte
  Wire.endTransmission();
  Wire.requestFrom((int)i2caddr, 1);

  //Wait until data is available, but not for more than timeout*10 ms
  while(!Wire.available() && timeout > 0) {
    timeout--;
    delay(10);
  }

  //Error
  if (!Wire.available()) {
    digitalWrite(IN_CIRCUIT_LED, LOW);
    return false;
  }

  *data = Wire.read();

  digitalWrite(IN_CIRCUIT_LED, LOW);

  return true;
}

//You should wait at least 5ms between two consequential function calls
void i2c_eeprom_write_byte(byte i2caddr, unsigned int byteaddr, byte data) {
  digitalWrite(IN_CIRCUIT_LED, HIGH);

  Wire.beginTransmission((int)i2caddr);
  /*
   * byteaddr is something like this: 0010 1100 0000 0111
   * first it will be sent the first byte (in the eg. 0010 1100)
   * by shifting right by 8 positions,
   * then it will be sent the rest of the address (0000 0111)
   * deleting the high-byte
   */
  byte high = (byte)((byteaddr >> 8) & 0x00FF);
  byte low = (byte)(byteaddr & 0x00FF);
  Wire.write(high); //Most significant Byte
  Wire.write(low); //Least significant Byte
  Wire.write(data);
  Wire.endTransmission();

  digitalWrite(IN_CIRCUIT_LED, LOW);
}

boolean i2c_eeprom_read_buffer(byte i2caddr, unsigned int byteaddr, byte data[], unsigned int length) {
  if (byteaddr + length >= 65536) {
    return false; //Invalid address range
  }

  boolean correct = true;
  for (unsigned int i = 0; i < length && correct; i++) {
    correct = i2c_eeprom_read_byte(i2caddr, byteaddr+i, data+i);
    delay(5);
  }

  return correct;
}

boolean i2c_eeprom_write_page(byte i2caddr, unsigned int byteaddr, byte data[], unsigned int length) {
  if (byteaddr + length >= 65536) {
    return false; //Invalid address range
  }

  for (unsigned int i = 0; i < length; i++) {
    i2c_eeprom_write_byte(i2caddr, byteaddr+i, data[i]);
    delay(5);
  }

  return true;
}

boolean i2c_eeprom_erase_bytes(byte i2caddr, unsigned int baseaddr, unsigned int length) {
  if (baseaddr + length >= 65536) {
    return false; //Invalid address range
  }

  digitalWrite(IN_CIRCUIT_LED, HIGH);

  for (unsigned int i = 0; i < length; i++) {
    i2c_eeprom_write_byte(EEPROM_ADDR, i+baseaddr, 0);
    delay(5);
  }

  digitalWrite(IN_CIRCUIT_LED, LOW);
  return true;
}
