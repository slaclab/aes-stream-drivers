/**
 *-----------------------------------------------------------------------------
 * Title         : Generic MCS File Reader
 * ----------------------------------------------------------------------------
 * File          : McsRead.cpp
 * Author        : Larry Ruckman  <ruckman@slac.stanford.edu>
 * Created       : 10/14/2013
 * Last update   : 08/11/2014
 *-----------------------------------------------------------------------------
 * Description :
 *    Generic MCS File reader
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

#include "McsRead.h"
#include <iostream>
#include <fstream>
#include <stdint.h>
#include <unistd.h>

using namespace std;

// Constructor
McsRead::McsRead ( ) {
}

// Deconstructor
McsRead::~McsRead ( ) {
}

// Open file
bool McsRead::open ( string filePath ) {

   promPntr = 16;
   promBaseAddr = 0;
   endOfFile = false;

   //attempt to open the file
   file.open(filePath.c_str() );

   //check if not opened
   if ( !file.is_open() ) {
      //show error message
      cout << "McsRead::open error = ";
      cout << "unable to open" << filePath << endl;

      //close the file
      close();

      //return error
      return false;
   }
   else {
      return true;
   }
}

//! Moves the ifstream to beginning of file
void McsRead::beg ( ) {
   promPntr = 16;
   promBaseAddr = 0;
   endOfFile = false;

   file.clear();
   file.seekg(0, ios::beg);
}

//! Open file
void McsRead::close ( ) {
   //close the file
   file.close();
}

//! Get Address space size file
uint32_t McsRead::startAddr ( ) {
   int32_t status = -1;
   beg();
   while( (status = next()) != 0);
   beg();
   return promAddr[0];
}

//! Get end address
uint32_t McsRead::endAddr ( ) {
   // Scan for end of file
   while(1) {
      // Check if need to read
      if (!endOfFile) {
         next();
      }
      //check if end of file
      if (endOfFile){
         break;
      }
   }
   return promLastAddr;
}

//! Get Address space size file
uint32_t McsRead::addrSize ( ) {
   // Return the difference
   return (endAddr() - startAddr());
}

// Get next memory data and address index
int32_t McsRead::read (McsReadData *mem) {
   int32_t status = 0;

   //check if we need to read the next line
   if(promPntr==16) {
      while(1) {
         //read the file if end of file is not detected
         if (!endOfFile) {
            status = next();
         }

         //check for end of file
         if (endOfFile){
            mem->endOfFile = endOfFile;
            return status;
         }
         //check for an error
         else if (status<0) {
            return status;
         }
         //check for a data read
         else if (status==0) {
            break;
         }
      }
   }

   //collect the data
   mem->address   = promAddr[promPntr];
   mem->data      = promData[promPntr];
   mem->endOfFile = false;

   //increment the pointer
   promPntr++;

   return 0;
}

// Get next data record
int32_t McsRead::next ( ) {
   string line;

   char dataChar[2+1];
   char summing = 0;
   char checkSum;

   uint32_t i;
   uint32_t byteCnt;
   uint32_t addr;
   uint32_t recordType;
   uint32_t data[16];

   //check the ifstream status flag
   if ( !file.good() ) {
      //show error message
      cout << "McsRead::next error = ";
      cout << "file.good = false" << endl;

      //return error
      return -1;
   }
   else{
      //readout a line
      getline(file,line);

      //check for "start code"
      if (line.at(0) != ':') {
         //show error message
         cout << "McsRead::next error = ";
         cout << "missing start code" << endl;
         cout << "\t line = "     << line << endl;
         //return error
         return -1;
      }
      else {
         //get byte count
         dataChar[0] = line.at(1);
         dataChar[1] = line.at(2);
         dataChar[2] = '\0';
         sscanf(dataChar, "%x", &byteCnt);
         summing += (char)(byteCnt);

         //get address index
         dataChar[0] = line.at(3);
         dataChar[1] = line.at(4);
         dataChar[2] = '\0';
         sscanf(dataChar, "%x", &data[0]);
         summing += (char)(data[0]);

         dataChar[0] = line.at(5);
         dataChar[1] = line.at(6);
         dataChar[2] = '\0';
         sscanf(dataChar, "%x", &data[1]);
         summing += (char)(data[1]);

         addr = (data[0] << 8) | data[1];

         //get record type
         dataChar[0] = line.at(7);
         dataChar[1] = line.at(8);
         dataChar[2] = '\0';
         sscanf(dataChar, "%x", &recordType);
         summing += (char)(recordType);

         //get the check sum in the line read
         dataChar[0] = line.at(9+(2*byteCnt));
         dataChar[1] = line.at(10+(2*byteCnt));
         dataChar[2] = '\0';
         sscanf(dataChar, "%x", &data[0]);
         checkSum = -1*(char)(data[0]);

         //check for an invalid byte count
         if (byteCnt>16) {
            //show error message
            cout << "McsRead::next error = ";
            cout << "Invalid byte count: ";
            cout << byteCnt << endl;
            cout << "\t line = "     << line << endl;
            return -1;
         }

         //cout << "byteCnt: " << byteCnt << endl;
         //cout << "addr: " << addr << endl;
         //cout << "recordType: " << recordType << endl;

         //check the record type
         switch ( recordType ) {
            case 0://data record

               //check for an invalid byte count
               if (byteCnt==0) {
                  //show error message
                  cout << "McsRead::next error = ";
                  cout << "Invalid byte count: ";
                  cout << byteCnt << endl;
                  cout << "\t line = "     << line << endl;
                  return -1;
               }
               //collect the data
               for(i=0;i<byteCnt;i++) {
                  dataChar[0] = line.at(9+(i*2));
                  dataChar[1] = line.at(10+(i*2));
                  dataChar[2] = '\0';
                  sscanf(dataChar, "%x", &data[i]);
                  summing += (char)(data[i]);
               }

               //cout << "summing = "  << (int32_t)summing << endl;
               //cout << "checkSum = " << (int32_t)checkSum << endl;

               //compare the check sums
               if( summing != checkSum ) {
                  //show error message
                  cout << "McsRead::next error = ";
                  cout << "CheckSum Error:  ";
                  cout << recordType << endl;
                  cout << "\t line = "     << line << endl;
                  cout << "\t summing = "  << (int32_t)summing << endl;
                  cout << "\t checkSum = " << (int32_t)checkSum << endl;
                  return -1;
               }

               //calculate the index pointer
               //because all all data fields are 16 bytes wide
               promPntr = 16 - byteCnt;

               //save the data and address indexes
               for(i=0;i<byteCnt;i++) {
                  promData[i+promPntr] = data[i];
                  promAddr[i+promPntr] = promBaseAddr + addr + i;
                  promLastAddr = promBaseAddr + addr + i;
                  // cout << "promAddr["<<i+promPntr<<"]: " << hex << promAddr[i+promPntr];
                  // cout << "\tpromData["<<i+promPntr<<"]: " << hex << promData[i+promPntr] << endl;
               }

               //return the record type
               return (int32_t)recordType;

            case 1://End Of File record

               //compare the check sums
               if( summing != checkSum ) {
                  //show error message
                  cout << "McsRead::next error = ";
                  cout << "CheckSum Error:  ";
                  cout << recordType << endl;
                  cout << "\t line = "     << line << endl;
                  cout << "\t summing = "  << (int32_t)summing << endl;
                  cout << "\t checkSum = " << (int32_t)checkSum << endl;
                  return -1;
               }

               //set the flag
               endOfFile = true;

               //return the record type
               return (int32_t)recordType;

            case 4://Extended Linear Address Record

               //check for an invalid byte count
               if (byteCnt!=2) {
                  //show error message
                  cout << "McsRead::next error = ";
                  cout << "Invalid byte count: ";
                  cout << byteCnt << endl;
                  cout << "\t line = " << line << endl;
                  return -1;
               }

               //check for an invalid address header
               if (addr!=0) {
                  //show error message
                  cout << "McsRead::next error = ";
                  cout << "Invalid address header: ";
                  cout << addr << endl;
                  cout << "\t line = " << line << endl;
                  return -1;
               }
               //collect the data
               for(i=0;i<byteCnt;i++) {
                  dataChar[0] = line.at(9+(i*2));
                  dataChar[1] = line.at(10+(i*2));
                  dataChar[2] = '\0';
                  sscanf(dataChar, "%x", &data[i]);
                  summing += (char)(data[i]);
               }

               //compare the check sums
               if( summing != checkSum ) {
                  //show error message
                  cout << "McsRead::next error = ";
                  cout << "CheckSum Error:  ";
                  cout << recordType << endl;
                  cout << "\t line = "     << line << endl;
                  cout << "\t summing = "  << (int32_t)summing << endl;
                  cout << "\t checkSum = " << (int32_t)checkSum << endl;
                  return -1;
               }

               //set the base address
               promBaseAddr = data[0];
               promBaseAddr = promBaseAddr << 8;
               promBaseAddr |= data[1];
               promBaseAddr = promBaseAddr << 16;

               //cout << "promBaseAddr: " << hex << promBaseAddr << endl;

               //return the record type
               return (int32_t)recordType;

            default:
               //show error message
               cout << "McsRead::next error = ";
               cout << "Invalid Record Type: ";
               cout << recordType << endl;
               cout << "\t line = " << line << endl;
               return -1;
         }
      }
   }
}
