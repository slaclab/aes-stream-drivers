/**
 *-----------------------------------------------------------------------------
 * Title         : TemCard PROM C++ Class
 * ----------------------------------------------------------------------------
 * File          : TemCardProm.cpp
 * Author        : Larry Ruckman  <ruckman@slac.stanford.edu>
 * Created       : 03/19/2014
 * Last update   : 08/11/2014
 *-----------------------------------------------------------------------------
 * Description :
 *    TemCard PROM C++ Class
 *-----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to 
 * the license terms in the LICENSE.txt file found in the top-level directory 
 * of this distribution and at: 
    * https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
 * No part of the aes_stream_drivers package, including this file, may be 
 * copied, modified, propagated, or distributed except according to the terms 
 * contained in the LICENSE.txt file.
 *-----------------------------------------------------------------------------
**/

#include <sstream>
#include <string>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <iomanip> 
#include <math.h>

#include <TemDriver.h>
#include "TemCardProm.h"
#include "McsRead.h"

using namespace std;

#define GEN3_BLOCK_SIZE    0x4000 // Assume the smallest block size of 16-kword/block
#define GEN3_PROM_SIZE     0x00947A5B
#define GEN3_CONFIG_REG    0xFD4F

#define GEN2_BLOCK_SIZE    0x10000
#define GEN2_PROM_SIZE     0x001ACD7F
#define GEN2_CONFIG_REG    0xBDDF

// Constructor
TemCardProm::TemCardProm (int32_t fd, string pathToFile ) {   

   // Set the file path
   _filePath = pathToFile;
   _fd       = fd;

   // Get card info
   temGetInfo(fd,&_info);

   // Determine version, use only lower 4 bits to get the base version
   _blockSize = GEN3_BLOCK_SIZE;
   _promSize  = GEN3_PROM_SIZE;
   _en        = _info.promPrgEn;
   writeToFlash(GEN3_CONFIG_REG,0x60,0x03);
}

// Deconstructor
TemCardProm::~TemCardProm ( ) { 
}

// CHeck if file exists
bool TemCardProm::fileExist() {
   ifstream ifile(_filePath.c_str());
   return ifile.good();
}

uint32_t TemCardProm::getPromSize () {
   McsRead mcsReader;
   uint32_t retVar;
   mcsReader.open(_filePath);
   printf("Calculating PROM file (.mcs) Memory Address size ...");    
   retVar = mcsReader.addrSize();
   printf("PROM Size = 0x%08x\n", retVar); 
   mcsReader.close();
   return retVar; 
}

//! Check for a valid firmware version  (true=valid firmware version)
bool TemCardProm::checkFirmwareVersion ( ) {
   printf("*******************************************************************\n");
   printf("Current Firmware Version on the FPGA: 0x%.8x\n",_info.version);
   if( getPromSize() != _promSize){
      printf("\t Invalid Prom Size\n");  
      return false;
   }else{
      return(_en);
   }
}

//! Print Power Cycle Reminder
void TemCardProm::rebootReminder ( ) {
   cout << "\n\n\n\n\n";
   cout << "***************************************" << endl;
   cout << "***************************************" << endl;
   cout << "A cold reboot or power cycle is required " << endl;
   cout << "to load the new firmware." << endl;
   cout << "***************************************" << endl;
   cout << "***************************************" << endl;
   cout << "\n\n\n\n\n";
}

//! Erase the PROM
void TemCardProm::eraseBootProm ( ) {

   uint32_t address = 0;
   double size = double(_promSize);
   double percentage;
   double skim = 5.0;    

   cout << "*******************************************************************" << endl;   
   cout << "Starting Erasing ..." << endl; 
   while(address<=_promSize) {       
      eraseCommand(address);
      address += _blockSize;
      percentage = (((double)address)/size)*100;
      if(percentage>=skim) {
         skim += 5.0;
         cout << "Erasing the PROM: " << floor(percentage) << " percent done" << endl;
      }               
   }   
   cout << "Erasing completed" << endl;
}

//! Write the .mcs file to the PROM
bool TemCardProm::writeBootProm ( ) {
   return(bufferedWriteBootProm());
}

//! Write the .mcs file to the PROM
bool TemCardProm::unbufferedWriteBootProm ( ) {
   cout << "*******************************************************************" << endl;
   cout << "Starting Writing ..." << endl; 
   McsRead mcsReader;
   McsReadData mem;
   
   uint32_t address = 0;  
   uint16_t fileData;
   double   size = double(_promSize);
   double   percentage;
   double   skim = 0.0; 
   bool     toggle = false;

   //check for valid file path
   if ( !mcsReader.open(_filePath) ) {
      mcsReader.close();
      cout << "mcsReader.close() = file path error" << endl;
      return false;
   }  
   
   //reset the flags
   mem.endOfFile = false;      
   
   //read the entire mcs file
   while(!mem.endOfFile) {
   
      //read a line of the mcs file
      if (mcsReader.read(&mem)<0){
         cout << "mcsReader.close() = line read error" << endl;
         mcsReader.close();
         return false;
      }
      
      // Check if this is the upper or lower byte
      if(!toggle) {
         toggle = true;
         fileData = (uint16_t)mem.data;
      } else {
         toggle = false;
         fileData |= ((uint16_t)mem.data << 8);
         programCommand(address,fileData);
         address++;
         percentage = (((double)address)/size)*100;
         percentage *= 2.0;//factor of two from two 8-bit reads for every write 16 bit write
         if(percentage>=skim) {
            skim += 5.0;
            cout << "Writing the PROM: " << percentage << " percent done" << endl;
         }         
      }
   }
   
   mcsReader.close();   
   cout << "Writing completed" << endl;   
   return true;
}

//! Write the .mcs file to the PROM
bool TemCardProm::bufferedWriteBootProm ( ) {
   cout << "*******************************************************************" << endl;
   cout << "Starting Writing ..." << endl; 
   McsRead mcsReader;
   McsReadData mem;
   
   uint32_t address = 0;  
   uint16_t fileData;
   uint16_t i;
   
   uint32_t bufAddr[256];  
   uint16_t bufData[256];   
   uint16_t bufSize = 0;
   
   double size = double(_promSize);
   double percentage;
   double skim = 5.0; 
   bool   toggle = false;

   //check for valid file path
   if ( !mcsReader.open(_filePath) ) {
      mcsReader.close();
      cout << "mcsReader.close() = file path error" << endl;
      return false;
   }  
   
   //reset the flags
   mem.endOfFile = false;      
   
   //read the entire mcs file
   while(!mem.endOfFile) {
   
      //read a line of the mcs file
      if (mcsReader.read(&mem)<0){
         cout << "mcsReader.close() = line read error" << endl;
         mcsReader.close();
         return false;
      }
      
      // Check if this is the upper or lower byte
      if(!toggle) {
         toggle = true;
         fileData = (uint16_t)mem.data;
      } else {
         toggle = false;
         fileData |= ((uint16_t)mem.data << 8);
         
         // Latch the values
         bufAddr[bufSize] = address;
         bufData[bufSize] = fileData;
         bufSize++;
         
         // Check if we need to send the buffer
         if(bufSize==256) {
            bufferedProgramCommand(bufAddr,bufData,bufSize);
            bufSize = 0;
         }

         address++;
         percentage = (((double)address)/size)*100;
         percentage *= 2.0;//factor of two from two 8-bit reads for every write 16 bit write
         if(percentage>=skim) {
            skim += 5.0;
            cout << "Writing the PROM: " << floor(percentage) << " percent done" << endl;
         }         
      }
   }
   
   // Check if we need to send the buffer
   if(bufSize != 0) {
      // Pad the end of the block with ones
      for(i=bufSize;i<256;i++){
         bufData[bufSize] = 0xFFFF;
      }
      // Send the last block program 
      bufferedProgramCommand(bufAddr,bufData,256);  
   }     
   
   mcsReader.close();   
   cout << "Writing completed" << endl;   
   return true;
}

//! Compare the .mcs file with the PROM (true=matches)
bool TemCardProm::verifyBootProm ( ) {
   cout << "*******************************************************************" << endl;
   cout << "Starting Verification ..." << endl; 
   McsRead mcsReader;
   McsReadData mem;
   
   uint32_t address = 0;  
   uint16_t promData,fileData;
   double size = double(_promSize);
   double percentage;
   double skim = 5.0; 
   bool   toggle = false;

   //check for valid file path
   if ( !mcsReader.open(_filePath) ) {
      mcsReader.close();
      cout << "mcsReader.close() = file path error" << endl;
      return(1);
   }  
   
   //reset the flags
   mem.endOfFile = false;   

   //read the entire mcs file
   while(!mem.endOfFile) {
   
      //read a line of the mcs file
      if (mcsReader.read(&mem)<0){
         cout << "mcsReader.close() = line read error" << endl;
         mcsReader.close();
         return false;
      }
      
      // Check if this is the upper or lower byte
      if(!toggle) {
         toggle = true;
         fileData = (uint16_t)mem.data;
      } else {
         toggle = false;
         fileData |= ((uint16_t)mem.data << 8);
         promData = readWordCommand(address);                
         if(fileData != promData) {
            cout << "verifyBootProm error = ";
            cout << "invalid read back" <<  endl;
            cout << hex << "\taddress: 0x"  << address << endl;
            cout << hex << "\tfileData: 0x" << fileData << endl;
            cout << hex << "\tpromData: 0x" << promData << endl;
            mcsReader.close();
            return false;
         }
         address++;
         percentage = (((double)address)/size)*100;
         percentage *= 2.0;//factore of two from two 8-bit reads for every write 16 bit write
         if(percentage>=skim) {
            skim += 5.0;
            cout << "Verifying the PROM: " << floor(percentage) << " percent done" << endl;
         }         
      }
   }
   
   mcsReader.close();  
   cout << "Verification completed" << endl;
   cout << "*******************************************************************" << endl;   
   return true;
}

//! Erase Command
void TemCardProm::eraseCommand(uint32_t address) {
   uint16_t status = 0;
   
   // Unlock the Block
   writeToFlash(address,0x60,0xD0);
   
   // Reset the status register
   writeToFlash(address,0x50,0x50);   
   
   // Send the erase command
   writeToFlash(address,0x20,0xD0);
   
   while(1) {
      // Get the status register
      status = readFlash(address,0x70);
      
      // Check for erasing failure
      if ( (status&0x20) != 0 ) {
      
         // Unlock the Block
         writeToFlash(address,0x60,0xD0);
         
         // Reset the status register
         writeToFlash(address,0x50,0x50);   
         
         // Send the erase command
         writeToFlash(address,0x20,0xD0);      
      
      // Check for FLASH not busy
      } else if ( (status&0x80) != 0 ) {
         break;
      }
   } 

   // Lock the Block
   writeToFlash(address,0x60,0x01);   
}

//! Program Command
void TemCardProm::programCommand(uint32_t address, uint16_t data) {
   uint16_t status = 0;
   
   // Unlock the Block
   writeToFlash(address,0x60,0xD0);
   
   // Reset the status register
   writeToFlash(address,0x50,0x50);   
   
   // Send the program command
   writeToFlash(address,0x40,data);   
   
   while(1) {
      // Get the status register
      status = readFlash(address,0x70);
      
      // Check for programming failure
      if ( (status&0x10) != 0 ) {
      
         // Unlock the Block
         writeToFlash(address,0x60,0xD0);
         
         // Reset the status register
         writeToFlash(address,0x50,0x50);   
         
         // Send the program command
         writeToFlash(address,0x40,data);     
      
      // Check for FLASH not busy
      } else if ( (status&0x80) != 0 ) {
         break;
      }
   } 

   // Lock the Block
   writeToFlash(address,0x60,0x01);   
}

//! Buffered Program Command
void TemCardProm::bufferedProgramCommand(uint32_t *address, uint16_t *data, uint16_t size) {
   uint16_t status = 0;
   uint16_t i;
   
   // Unlock the Block
   writeToFlash(address[0],0x60,0xD0);
   
   // Reset the status register
   writeToFlash(address[0],0x50,0x50);

   // Send the buffer program command and size
   writeToFlash(address[0],0xE8,(size-1));   
   
   // Load the buffer
   for(i=0;i<size;i++) {
      readFlash(address[i],data[i]);
   }
  
   // Confirm buffer programming
   readFlash(address[0],0xD0);  
   
   while(1) {
      // Get the status register
      status = readFlash(address[0],0x70);
      
      // Check for programming failure
      if ( (status&0x10) != 0 ) {
      
         // Unlock the Block
         writeToFlash(address[0],0x60,0xD0);
         
         // Reset the status register
         writeToFlash(address[0],0x50,0x50);   
         
         // Send the buffer program command and size
         writeToFlash(address[0],0xE8,(size-1));   
         
         // Load the buffer
         for(i=0;i<size;i++) {
            readFlash(address[i],data[i]);
         }
        
         // Confirm buffer programming
         readFlash(address[0],0xD0);                    
      
      // Check for FLASH not busy
      } else if ( (status&0x80) != 0 ) {
         break;
      }
   } 

   // Lock the Block
   writeToFlash(address[0],0x60,0x01);   
}

//! Read FLASH memory Command
uint16_t TemCardProm::readWordCommand(uint32_t address) {
   return readFlash(address,0xFF);
}

//! Generic FLASH write Command 
void TemCardProm::writeToFlash(uint32_t address, uint16_t cmd, uint16_t data) {
   temWriteProm(_fd,address,cmd,data);
}

//! Generic FLASH read Command
uint16_t TemCardProm::readFlash(uint32_t address, uint16_t cmd) {
   uint32_t data;
   temReadProm(_fd,address,cmd,&data);
   return(data&0xFFFF);
}

