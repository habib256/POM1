/*
   +-----------+                     +-----------------+ 
   |           |                     |     ARDUINO     |
   | VIA 6522  |                     |      NANO       |
   |           |                     |                 |
   |       PA0 |<------ bit 0 ------>| D2              |                +-------\ 
   |       PA1 |<------ bit 1 ------>| D3          D10 |----- SS ------>|        \
   |       PA2 |<------ bit 2 ------>| D4          D11 |----- MOSI ---->|  SD    |
   |       PA3 |<------ bit 3 ------>| D5          D12 |<---- MISO -----|  CARD  |
   |       PA4 |<------ bit 4 ------>| D6          D13 |----- SCK ----->|        |
   |       PA5 |<------ bit 5 ------>| D7              |                +--------+
   |       PA6 |<------ bit 6 ------>| D8              |
   |       PA7 |<------ bit 7 ------>| D9              |
   |           |                     |                 |
   |       PB7 |<---- MCU_STROBE ----| D14/A0          |
   |       PB0 |--- CPU_STROBE ----->| D15/A1          |
   |           |                     |                 |
   +-----------+                     +-----------------+ 
*/

/*
PORT ASSIGNMENT FOR FAST READ/WRITE
===================================
BIT 0:      PORTD[2]
BIT 1:      PORTD[3]
BIT 2:      PORTD[4]
BIT 3:      PORTD[5]
BIT 4:      PORTD[6]
BIT 5:      PORTD[7]
BIT 6:      PORTB[0]
BIT 7:      PORTB[1]
MCU_STROBE: PORTC[0]
CPU_STROBE: PORTC[1]

in Verilog syntax: data = { PORTB[1:0], PORTD[7:2] };

*/

// FASTWRITE not working (yet)
// #define FASTWRITE 1

#ifdef FASTWRITE
#define get_cpu_strobe    ((PORTC >> 1) & 1)
#define set_mcu_strobe(c) PORTC = (PORTC & 0xFE) | (c)
#else
#define get_cpu_strobe digitalRead(CPU_STROBE)
#define set_mcu_strobe(c) digitalWrite(MCU_STROBE,(c))
#endif

#include <SPI.h>
#include "SdFat.h"

SdFat SD;

#define SD_CS_PIN SS

// pin definitions

#define BIT0 2   
#define BIT1 3   
#define BIT2 4   
#define BIT3 5   
#define BIT4 6   
#define BIT5 7   
#define BIT6 8   
#define BIT7 9   

/* 10,11,12,13 reserved for SD card */

#define MCU_STROBE  14   
#define CPU_STROBE  15   

// indicates that a timeout occurred during wait_cpu_strobe()
byte TIMEOUT = 0;

void wait_cpu_strobe(byte value) {
    
  unsigned long start_time = millis();  
  unsigned long elapsed;  

  if(TIMEOUT) return;
  
  while(get_cpu_strobe != value) {
    elapsed = millis() - start_time;
    if(elapsed > 500) {
      TIMEOUT = 1;
      break;
    }      
  }
}

const byte DIR_INPUT  = 0;
const byte DIR_OUTPUT = 1;

byte last_dir; // remember last data port pins direction 

void set_data_port_direction(byte dir) {

  // check if no need to set pins
  if(dir == last_dir) return;
  
  if(dir == DIR_INPUT) {
    pinMode(BIT0, INPUT);
    pinMode(BIT1, INPUT);
    pinMode(BIT2, INPUT);
    pinMode(BIT3, INPUT);
    pinMode(BIT4, INPUT);
    pinMode(BIT5, INPUT);
    pinMode(BIT6, INPUT);
    pinMode(BIT7, INPUT);     
  }
  else {
    pinMode(BIT0, OUTPUT);
    pinMode(BIT1, OUTPUT);
    pinMode(BIT2, OUTPUT);
    pinMode(BIT3, OUTPUT);
    pinMode(BIT4, OUTPUT);
    pinMode(BIT5, OUTPUT);
    pinMode(BIT6, OUTPUT);
    pinMode(BIT7, OUTPUT);
  }  

  // remember direction
  last_dir = dir;
}

byte receive_byte_from_cpu() {

  // set data port pins as INPUT pins
  set_data_port_direction(DIR_INPUT);
  
  // both strobes are 0   
  
  // CPU deposits data byte and sets strobe high
  wait_cpu_strobe(HIGH);           
  
  // read the data byte
  #ifdef FASTWRITE  
  byte data = ((PORTB & 0x03)<<6) | (PORTD >> 2);  // data = { PORTB[1:0], PORTD[7:2] }
  #else
  byte data = 
    (digitalRead(BIT0) << 0) |
    (digitalRead(BIT1) << 1) |
    (digitalRead(BIT2) << 2) |
    (digitalRead(BIT3) << 3) |
    (digitalRead(BIT4) << 4) |
    (digitalRead(BIT5) << 5) |
    (digitalRead(BIT6) << 6) |
    (digitalRead(BIT7) << 7);
  #endif 

  // after reading the byte, MCU sets strobe high
  set_mcu_strobe(HIGH);

  // CPU now sets strobe low
  wait_cpu_strobe(LOW);

  // and MCU sets strobe low
  set_mcu_strobe(LOW);
    
  return data;
}

void send_byte_to_cpu(byte data) {

  // set data port pins as OUTPUT pins
  set_data_port_direction(DIR_OUTPUT);

  // both strobes are 0
  
  // put byte on the data port
  #ifdef FASTWRITE  
  PORTB = (PORTB & 0xFC) | ((data >> 6) & 0x03);   // PORTB[1:0] = data[7:6];
  PORTD = (PORTD & 0x03) | (data << 2);            // PORTD[7:2] = data[5:0];  
  #else
  digitalWrite(BIT0, data &   1);
  digitalWrite(BIT1, data &   2);
  digitalWrite(BIT2, data &   4);
  digitalWrite(BIT3, data &   8);
  digitalWrite(BIT4, data &  16);
  digitalWrite(BIT5, data &  32);
  digitalWrite(BIT6, data &  64);
  digitalWrite(BIT7, data & 128);
  #endif
  
  // after depositing data byte, MCU sets strobe high
  set_mcu_strobe(HIGH);
  
  // wait for CPU to set strobe high
  wait_cpu_strobe(HIGH);  
  
  // tells CPU byte we are finished
  set_mcu_strobe(LOW); 

  // wait for CPU to set strobe low
  wait_cpu_strobe(LOW);  
}

// *********************************************************************************************
// *********************************************************************************************
// *********************************************************************************************
// *********************************************************************************************
// *********************************************************************************************
// *********************************************************************************************
// *********************************************************************************************
// *********************************************************************************************

const byte CMD_READ  =  0;
const byte CMD_WRITE =  1;
const byte CMD_DIR   =  2;
const byte CMD_LOAD  =  4;
const byte CMD_DEL   = 11;
const byte CMD_LS    = 12;
const byte CMD_CD    = 13;
const byte CMD_MKDIR = 14;
const byte CMD_PWD   = 19;
const byte CMD_RMDIR = 15;
const byte CMD_TEST  = 20;
const byte CMD_MOUNT = 23;

const byte ERR_RESPONSE  = 255;
const byte WAIT_RESPONSE =   1;
const byte OK_RESPONSE   =   0;

// @buffer
char filename[64]; 
char tmp[64];
char cd_path[64];

// fixed messages
const char *NOT_A_FILE = "?NOT A FILE";
const char *FILE_NOT_FOUND = "?FILE NOT FOUND";
const char *CANT_OPEN_FILE = "?CAN'T OPEN FILE";
const char *ALREADY_EXISTS = "?FILE ALREADY EXISTS";
const char *CANT_CREATE_FILE = "?CAN'T CREATE FILE";
const char *WRITE_ERROR = "?WRITE ERROR";
const char *CANT_DELETE_FILE = "?CAN'T DELETE FILE";
const char *FILE_DELETED = " DELETED";
const char *DIR_NOT_FOUND = "?DIR NOT FOUND";
const char *CANT_REMOVE_DIR = "?CAN'T REMOVE DIR";
const char *DIR_REMOVED = " (DIR) REMOVED";
const char *DIR_ALREADY_EXISTS = "?DIR ALREADY EXISTS";
const char *CANT_MAKE_DIR = "?CAN'T MAKE DIR";
const char *DIR_CREATED = " (DIR) CREATED";
const char *CANT_CD_DIR = "?CAN'T CHANGE DIR";
const char *NOT_A_DIRECTORY = "?NOT A DIRECTORY";
const char *MOUNT_OK = "MOUNTING SDCARD...\rOK";
const char *MOUNT_FAILED = "?SD CARD ERROR";

// file structures used to operate with the SD card
File myFile;
File myDir;

void setup() {
  // debug on serial
  Serial.begin(9600);  
  Serial.println(F("SDCARD library: SDFat.h"));

  mount_sdcard();
  
  // control pins setup
  pinMode(CPU_STROBE, INPUT);
  pinMode(MCU_STROBE, OUTPUT);  
  digitalWrite(MCU_STROBE, LOW);   

  // data pins setup
  last_dir = -1;  // no previous data direction  
  set_data_port_direction(DIR_INPUT);

  // set working directory to root
  strcpy(cd_path, "/");
}

bool mount_sdcard() {
  // initialize SD card
  if(!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD card initialization failed"));
    return false;
  }
  else {
    Serial.println(F("SD card initialized"));
    return true;
  }
}

// **************************************************************************************
// **************************************************************************************
// ********************************* @CMD_DIR  ******************************************
// **************************************************************************************
// **************************************************************************************

byte list_files;
void comando_dir(byte command) {
  Serial.println(F("command DIR received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("dir to read: "));
  Serial.println(filename);  
  
  if(filename[0]==0) {
    // no argument given, use cd_path
    strcpy(filename, cd_path);    
  }

  myDir = SD.open(filename);

  char file_path[64];
  char pattern[64];
  // check if directory exist and is a directory
  if(!myDir || !myDir.isDirectory()) {
    // not found, use the given filename as a pattern
    split_path(filename, file_path, pattern);
    if(file_path[0]==0) {
      // no path given, use cd_path
      strcpy(file_path, cd_path);    
    }  
    myDir = SD.open(file_path);    
    if(!myDir || !myDir.isDirectory()) {
      Serial.println(F("dir not found"));    
      send_byte_to_cpu(ERR_RESPONSE);
      send_string_to_cpu(DIR_NOT_FOUND);
      return;    
    }    
  }
  else {
    pattern[0] = 0;  // argument was a valid directory, use no pattern
  }  

  // dir opened OK
  send_byte_to_cpu(OK_RESPONSE);

  list_files = 0;  // starts listing dirs (0) then files (1)

  while(true) {
    byte c = receive_byte_from_cpu();
    if(TIMEOUT) {
      Serial.println(F("timeout while receiving 'next line' byte"));    
      break; 
    } else if(c == WAIT_RESPONSE) {
      // do nothing, just keep listening
      continue;   
    } else if(c == ERR_RESPONSE) {
      // user asked to abort dir listing
      Serial.println(F("dir interrupted by the user"));
      break;       
    } else if(c == OK_RESPONSE) {
      if(!send_next_dir_line(command, pattern)) {
        Serial.println(F("dir terminated"));
        break;
      }
    }
  }
  myDir.close();
}

byte send_next_dir_line(byte command, char *pattern) {
  // user wants a new line of dir listing

  while(1) {
    myFile = myDir.openNextFile();
    
    if(!myFile) {
      if(list_files==0) {
        list_files++;
        myDir.rewind();
        continue;
      }
      else {
        // no more lines to send       
        myDir.close(); 
        send_byte_to_cpu(ERR_RESPONSE);   
        return 0; // no more lines
      }
    }

    if(myFile.isDirectory() && list_files == 0) break;
    if(!myFile.isDirectory() && list_files == 1) break;
  }
  
  // send the text line 
  send_byte_to_cpu(OK_RESPONSE);    
  send_directory_entry(command, pattern);                  
  myFile.close(); 
  return 1; // more lines to send
}

void send_directory_entry(byte command, char *pattern) {
  myFile.getName(filename, 64);
  strtoupper(filename);

  // verify pattern matching if any
  if(pattern[0] != 0) {
      int len = strlen(pattern);
      bool match_found = strncmp(filename, pattern, len) == 0;
      if(!match_found) {
        send_byte_to_cpu(0); // empty line
        return;
      }
  }
    
  if(myFile.isDirectory()) {
    if(command != CMD_DIR) {
      sprintf(tmp, "(DIR) %s\r", filename);
      print_string_to_cpu(tmp);
      Serial.println(tmp);     
    }  
    else {
      sprintf(tmp, "%-15s (DIR)\r", filename);
      print_string_to_cpu(tmp);
      Serial.println(tmp);     
    }                
  }
  else {
    if(command != CMD_DIR) {
      // BUG the following line does not work
      // sprintf(tmp, "%5d %s", entry.size(), filename);        
      // use this instead
      sprintf(tmp, "%5d ", myFile.size());
      print_string_to_cpu(tmp);
      Serial.print(tmp);       

      print_string_to_cpu(filename);                
      Serial.println(filename);               

      send_byte_to_cpu('\r');           
    }
    else {                            
      char type[5];
      char address[5];      
      
      strcpy(type,"");
      strcpy(address, ""); 

      char *x = strchr(filename, '#');
      if(x != NULL) {
        *x++ = 0;              
        if(x[0]=='0' && x[1]=='6') {
            strcpy(type,"BIN ");
            strcpy(address,x+2);  
        }
        else if(x[0]=='F' && x[1]=='1') {
            strcpy(type,"BAS ");
            strcpy(address,x+2);  
        }        
        else if(x[0]=='F' && x[1]=='8') {
            strcpy(type,"ASB ");
            strcpy(address,x+2);
        }
        else {
            type[0] = '#';
            type[1] = x[0];
            type[2] = x[1];
            strcpy(address,x+2);
        }
      }
                            
      sprintf(tmp, "%-15s", filename);
      print_string_to_cpu(tmp);                              
      Serial.print(filename);               

      sprintf(tmp, "%6d ", myFile.size());
      print_string_to_cpu(tmp);
      Serial.print(tmp);

      print_string_to_cpu(type);
      Serial.print(type);

      if(type[0]!=0) {
        send_byte_to_cpu('$');  
        Serial.print("$");      
      }
      print_string_to_cpu(address);     
      Serial.println(address);

      send_byte_to_cpu('\r');
    }
  }            
}

// **************************************************************************************
// **************************************************************************************
// ********************************* CMD_READ  ******************************************
// **************************************************************************************
// **************************************************************************************

void comando_read() {
  Serial.println(F("command CMD_READ received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("file to read: "));
  Serial.println(filename);

  if(!SD.exists(filename)) {
    Serial.println(F("error opening file"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(FILE_NOT_FOUND);
    return;    
  }

  // open the file 
  myFile = SD.open(filename);
  if(!myFile) {            
    Serial.println(F("error opening file"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(CANT_OPEN_FILE);
    return;
  }

  if(myFile.isDirectory()) {
    myFile.close();
    Serial.println(F("not a file"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(NOT_A_FILE);
    return;
  }

  Serial.println(F("file opened on the SD card"));

  // ok response
  send_byte_to_cpu(OK_RESPONSE);
  if(TIMEOUT) return;
  Serial.println(F("ok response sent to CPU"));
    
  // sends size as low and high byte
  int size = myFile.size();
  send_byte_to_cpu(size & 0xFF);
  send_byte_to_cpu((size >> 8) & 0xFF);
  if(TIMEOUT) return;
  Serial.println(F("file size sent to CPU"));

  int bytes_sent = 0;    
  while(myFile.available() && !TIMEOUT) {      
    send_byte_to_cpu(myFile.read());
    if(!TIMEOUT) bytes_sent++;
  }    
  myFile.close();

  if(TIMEOUT) {
    Serial.print(F("timeout, bytes sent: ")); 
    Serial.println(bytes_sent); 
    return;
  }
    
  Serial.println(F("file read ok"));
}

void send_string_to_cpu(char *msg) {
  while(1) {
    byte c = *msg++;
    send_byte_to_cpu(c);
    if(TIMEOUT) break;
    if(c==0) break;
  }
}

// come send_string_to_cpu ma senza lo zero finale
void print_string_to_cpu(char *msg) {
  while(1) {
    byte c = *msg++;
    if(c==0) break;
    send_byte_to_cpu(c);
    if(TIMEOUT) break;    
  }
}


void receive_string_from_cpu(char *msg) {
  while(1) {
    byte c = receive_byte_from_cpu();
    if(TIMEOUT) break;    
    *msg++ = c;
    if(c==0) break;
  }  
}

// **************************************************************************************
// **************************************************************************************
// ********************************* CMD_WRITE  *****************************************
// **************************************************************************************
// **************************************************************************************

int receive_word_from_cpu() {
  byte lo = receive_byte_from_cpu();
  byte hi = receive_byte_from_cpu();
  int data = lo | (hi << 8);  
  return data;
}

void comando_write() {
  Serial.println(F("command CMD_WRITE received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("file to write: "));
  Serial.println(filename);
  
  if(SD.exists(filename)) {
    Serial.println(F("file already exist"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(ALREADY_EXISTS);
    return;    
  }

  // open the file 
  myFile = SD.open(filename, FILE_WRITE);
  if(!myFile) {            
    Serial.println(F("error opening file for write"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(CANT_CREATE_FILE);
    return;
  }  
  Serial.println(F("file opened for write on the SD card"));

  // ok response
  send_byte_to_cpu(OK_RESPONSE);
  if(TIMEOUT) return;
  Serial.println(F("first ok response sent to CPU"));
    
  // get file size low and high byte
  int size = receive_word_from_cpu();
  if(TIMEOUT) return;
  Serial.print(F("received file size: "));
  Serial.println(size);

  byte error = 0;
  for(int t=0;t<size;t++) {
    byte data = receive_byte_from_cpu();
    if(TIMEOUT) break;
    int n = myFile.write((unsigned char)data);
    if(n!=1) error = 1;
  }

  // close the file
  myFile.close();

  if(TIMEOUT) return;

  // report write issues
  if(error) {
    Serial.println(F("file write error"));    
    send_byte_to_cpu(ERR_RESPONSE);
    if(TIMEOUT) return;
    send_string_to_cpu(WRITE_ERROR);
    return;
  }
      
  Serial.println(F("file read ok"));
  send_byte_to_cpu(OK_RESPONSE);
}

// **************************************************************************************
// **************************************************************************************
// ********************************* CMD_LOAD  ******************************************
// **************************************************************************************
// **************************************************************************************

void comando_load() {
  Serial.println(F("command CMD_LOAD received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("file to read: "));
  Serial.println(filename);

  // if a matching file name is found, use it
  if(matchname(filename, tmp)==1) {
    Serial.print(F("found matching file: "));
    Serial.println(tmp);
    strcpy(filename, tmp);
  }

  if(!SD.exists(filename)) {
    Serial.println(F("error opening file"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(FILE_NOT_FOUND);
    return;    
  }

  // open the file 
  myFile = SD.open(filename);
  if(!myFile) {            
    Serial.println(F("error opening file"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(CANT_OPEN_FILE);
    return;
  }

  if(myFile.isDirectory()) {
    myFile.close();
    Serial.println(F("not a file"));
    send_byte_to_cpu(ERR_RESPONSE);
    send_string_to_cpu(NOT_A_FILE);
    return;
  }

  Serial.println(F("file opened on the SD card"));

  // ok response
  send_byte_to_cpu(OK_RESPONSE);
  if(TIMEOUT) return;
  Serial.println(F("ok response sent to CPU"));

  // sends matched filename
  send_string_to_cpu(filename);
    
  // sends size as low and high byte
  int size = myFile.size();
  send_byte_to_cpu(size & 0xFF);
  send_byte_to_cpu((size >> 8) & 0xFF);
  if(TIMEOUT) return;
  Serial.println(F("file size sent to CPU"));

  int bytes_sent = 0;    
  while(myFile.available() && !TIMEOUT) {      
    send_byte_to_cpu(myFile.read());
    if(!TIMEOUT) bytes_sent++;
  }    
  myFile.close();

  if(TIMEOUT) {
    Serial.print(F("timeout, bytes sent: ")); 
    Serial.println(bytes_sent); 
    return;
  }
    
  Serial.println(F("file read ok"));
}

void strtoupper(char *str){
      int len = strlen(str), i;
       
      for(i=0;i<len;i++)
            if(str[i]>='a' && str[i]<='z')
                 str[i] = str[i]-'a'+'A';
}

// splits filename into file_path and file_name
// e.g. "root/myfolder/pluto" => "root/myfolder" , "pluto"
//      "myfolder/pluto" => "myfolder", "pluto"
//      "pluto" => "", "pluto"
//      "/pluto" => "/", "pluto"

void split_path(char *filename, char *file_path, char *file_name) {
  strcpy(file_path, filename);
  
  for(int t=strlen(file_path)-1;t>0;t--) {
    if(file_path[t] == '/') {
       file_path[t] = 0;
       strcpy(file_name, &file_path[t+1]);  
       if(t==0) strcpy(file_path, "/");  // case of root folder
       return;
    }
  }
  strcpy(file_path, "");
  strcpy(file_name, filename);  
}

//
// returns in dest the first file that matches (starts with) "filename"
// returns 1 if matching file is found
// returns 0 if no matching file is found
//
byte matchname(char *filename, char *dest) {
  // @buffer
  char file_path[64];
  char file_name[64];

  // split filename into file_path and file_name
  split_path(filename, file_path, file_name);

  Serial.print(F("after split file_path="));
  Serial.print(file_path);
  Serial.print(F(", file_name="));
  Serial.println(file_name);

  if(strlen(file_path)==0) Serial.println(F("scanning the current directory"));
  else                     Serial.println(F("scanning the file_path directory"));

  // open the directory containing the file
  myDir = SD.open(strlen(file_path)==0 ? cd_path : file_path);

  if(!myDir) return 0;

  Serial.println(F("dir opened"));

  // scans twice: scanmode=0 for exact match, scanmode=1 for partial match
  for(byte scanmode=0;scanmode<=1;scanmode++) {
    if(scanmode==0) Serial.println(F("scanning for exact match..."));
    if(scanmode==1) Serial.println(F("scanning for partial match..."));
   
    // scan all the directory
    while(1) {

      send_byte_to_cpu(WAIT_RESPONSE);
      if(TIMEOUT) return 0;        
      
      myFile = myDir.openNextFile();
      
      // end of directory
      if(!myFile) {
        if(scanmode == 0) {
          // exact match not found, rewind the directory and scan fot partial match
          myDir.rewind();
          break;
        }
        else if(scanmode == 1) {
          // match not found even with partial match, return error
          myDir.close();
          return 0;      
        }
      }

      // copy filename in dest and closes it
      myFile.getName(dest, 64);
      strtoupper(dest);
      myFile.close();

      Serial.print(F("file entry: "));
      Serial.println(dest);

      // verify the match
      int len = strlen(file_name);
      bool match_found = strncmp(file_name, dest, len) == 0;

      // enforce exact match
      if(scanmode == 0) match_found = match_found && dest[strlen(file_name)] == '#';

      if(match_found) {
        if(scanmode==0) Serial.println(F("found an exact match"));
        if(scanmode==1) Serial.println(F("found a partial match"));

        // matching, dest already contains the matched file name

        // if file_path is empty then it's current directory, do nothing
        // else file_path needs to be combined with file name
        if(file_path[0]!=0) {
          Serial.println(F("not on current dir, joining paths"));
          strcpy(filename, dest);
          if(file_path[0]=='/' && file_path[1]==0)  sprintf(dest,"/%s", filename);              // case of root folder
          else                                      sprintf(dest,"%s/%s", file_path, filename); // case of normal nested folder
          //Serial.println(filename);
          //Serial.println(file_path);
          //Serial.println(dest);
        }
        myDir.close();
        return 1;           
      }    
    }
  }     
}

// **************************************************************************************
// **************************************************************************************
// ********************************* CMD_DEL ********************************************
// **************************************************************************************
// **************************************************************************************

void comando_del() {
  Serial.println(F("command DEL received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("file to delete: "));
  Serial.println(filename);
  
  if(!SD.exists(filename)) {
    Serial.println(F("file does not exist"));
    send_string_to_cpu(FILE_NOT_FOUND);
    return;    
  }

  if(TIMEOUT) return;

  // open the file for deletion
  myFile = SD.open(filename, FILE_WRITE);
  if(!myFile) {            
    Serial.println(F("error opening file for deletion"));    
    send_string_to_cpu(CANT_DELETE_FILE);
    myFile.close();
    return;
  }  

  if(TIMEOUT) return;
    
  if(!myFile.remove()) {
    Serial.println(F("error removing file"));
    send_string_to_cpu(CANT_DELETE_FILE);      
    myFile.close();
    return;
  }

  // close the file
  myFile.close();

  print_string_to_cpu(filename);  
  send_string_to_cpu(FILE_DELETED); 

  Serial.println(F("file deleted"));       
}

// **************************************************************************************
// **************************************************************************************
// ********************************* CMD_RMDIR ******************************************
// **************************************************************************************
// **************************************************************************************

void comando_rmdir() {
  Serial.println(F("command RMDIR received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("directory to delete: "));
  Serial.println(filename);

  if(TIMEOUT) return;

  // check if the directory exists
  if(!SD.exists(filename)) {
    Serial.println(F("dir does not exist"));
    send_string_to_cpu(DIR_NOT_FOUND);
    return;
  }

  // removes the directory
  if(!SD.rmdir(filename)) {
    Serial.println(F("error removing dir"));
    send_string_to_cpu(CANT_REMOVE_DIR);
    return;
  }

  print_string_to_cpu(filename);
  send_string_to_cpu(DIR_REMOVED);

  Serial.println(F("dir removed"));
}

// **************************************************************************************
// **************************************************************************************
// ********************************* CMD_MKDIR ******************************************
// **************************************************************************************
// **************************************************************************************

void comando_mkdir() {
  Serial.println(F("command MKDIR received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("directory to create: "));
  Serial.println(filename);

  if(TIMEOUT) return;

  // check if the directory exists
  if(SD.exists(filename)) {
    Serial.println(F("dir already exist"));
    send_string_to_cpu(DIR_ALREADY_EXISTS);
    return;
  }

  // removes the directory
  if(!SD.mkdir(filename)) {
    Serial.println(F("error creating"));
    send_string_to_cpu(CANT_MAKE_DIR);
    return;
  }

  print_string_to_cpu(filename);
  send_string_to_cpu(DIR_CREATED);

  Serial.println(F("dir created"));
}

// **************************************************************************************
// **************************************************************************************
// ********************************* @CMD_CD ********************************************
// **************************************************************************************
// **************************************************************************************

void comando_cd() {
  Serial.println(F("command CD received from CPU"));

  // reads filename as 0 terminated string
  receive_string_from_cpu(filename);
  if(TIMEOUT) return;
  Serial.print(F("directory to change to: "));
  Serial.println(filename);

  if(TIMEOUT) return;

  // CD without arguments
  if(filename[0] == 0) {
    // ok response
    send_byte_to_cpu(OK_RESPONSE);
    return;
  }

  // CD ..
  if(filename[0] == '.' && filename[1] == '.' && filename[2] == 0) {  
    strcpy(filename, cd_path);
    for(int t=strlen(filename)-1;t>=1;t--) {
      byte c = filename[t];
      filename[t] = 0;
      if(c == '/') break;        
    }
  }

  // CD /
  if(filename[0] == '/' && filename[1] == 0) {
    // changes to the root directory
    if(SD.chdir()) {
      // set root working directory
      strcpy(cd_path, filename);      
      Serial.print(F("dir changed to:"));
      Serial.println(cd_path);
      send_byte_to_cpu(OK_RESPONSE);
      return;      
    }   
    else {
      Serial.println(F("error changing dir"));
      send_byte_to_cpu(ERR_RESPONSE);
      send_string_to_cpu(CANT_CD_DIR);
      return;
    }
  }

  // CD dirname  
  if(SD.chdir(filename)) {
    // update working directory
    if(filename[0] == '/') {       
      strcpy(cd_path, filename);     // path is absolute, replace cwd   
    }
    else {
      // path is relative, append to cwd
      if(cd_path[0] == '/' && cd_path[1] == 0) {        
        cd_path[0] = 0;  // avoid the double slash ("//DIR") when cwd is root 
      }
      sprintf(tmp, "%s/%s", cd_path, filename);
      strcpy(cd_path, tmp);
    }
    
    
    Serial.print(F("dir changed to:"));
    Serial.println(cd_path);    
    send_byte_to_cpu(OK_RESPONSE);
    return;
  } 

  // errors 

  send_byte_to_cpu(ERR_RESPONSE);
  
  // check if the directory exists
  if(!SD.exists(filename)) {
    Serial.println(F("dir not found"));    
    send_string_to_cpu(DIR_NOT_FOUND);
    return;
  }
  
  myFile = SD.open(filename);
  bool isDir = myFile.isDirectory();
  myFile.close();
  
  if(!isDir) {            
    Serial.println(F("not a directory"));    
    send_string_to_cpu(NOT_A_DIRECTORY);    
  }
  else {
    Serial.println(F("error changing dir"));    
    send_string_to_cpu(CANT_CD_DIR);    
  }
}

// **************************************************************************************
// **************************************************************************************
// ********************************* @CMD_PWD *******************************************
// **************************************************************************************
// **************************************************************************************

void comando_pwd() {
  Serial.println(F("command PWD received from CPU"));
  send_string_to_cpu(cd_path);
}

// **************************************************************************************
// **************************************************************************************
// ********************************* LOOP  **********************************************
// **************************************************************************************
// **************************************************************************************

void loop() {  
  TIMEOUT = 0;

  byte data = receive_byte_from_cpu();
  if(TIMEOUT) return;

  unsigned long start_time = millis();

       if(data == CMD_READ)  comando_read();      
  else if(data == CMD_WRITE) comando_write();      
  else if(data == CMD_LOAD)  comando_load();      
  else if(data == CMD_DEL)   comando_del();    
  else if(data == CMD_RMDIR) comando_rmdir();    
  else if(data == CMD_MKDIR) comando_mkdir();     
  else if(data == CMD_CD)    comando_cd();      
  else if(data == CMD_PWD)   comando_pwd();      
  else if(data == CMD_DIR)   comando_dir(data);    
  else if(data == CMD_LS)    comando_dir(data);
  else if(data == CMD_TEST) {
    while(!TIMEOUT) {
      byte data = receive_byte_from_cpu();
      send_byte_to_cpu(data ^ 0xFF);
    }
  }
  else if(data == CMD_MOUNT) {
    bool mount = mount_sdcard();
    if(mount) send_string_to_cpu(MOUNT_OK);
    else send_string_to_cpu(MOUNT_FAILED);
  }
  else {
    Serial.print(F("unknown command "));
    Serial.print(data);
    Serial.println(F(" received"));
  }

  Serial.print(F("command processing time: "));
  Serial.println(millis() - start_time);
  
  if(TIMEOUT) Serial.println(F("TIMEOUT during command"));    
}
