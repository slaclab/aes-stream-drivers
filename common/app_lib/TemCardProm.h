/**
 *-----------------------------------------------------------------------------
 * Title         : TemCard PROM C++ Class
 * ----------------------------------------------------------------------------
 * File          : TemCardProm.h
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

#ifndef __TEMCARD_PROM_H__
#define __TEMCARD_PROM_H__

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/types.h>

#include <string.h>
#include <stdint.h>
#include <TemDriver.h>

using namespace std;

//! Class to contain generic register data.
class TemCardProm {
   public:

      //! Constructor
      TemCardProm (int32_t fd, string pathToFile );

      //! Deconstructor
      ~TemCardProm ( );
      
      //! Get the PROM size
      uint32_t getPromSize ();

      //! Check and configure board parameters
      bool checkFirmwareVersion ();

      //! Check if file exists
      bool fileExist ();

      //! Erase the PROM
      void eraseBootProm ( );    

      //! Write the .mcs file to the PROM
      bool writeBootProm ( );   

      //! Compare the .mcs file with the PROM
      bool verifyBootProm ( );     

      //! Print Reminder
      void rebootReminder ( );      
   
   private:
      // Local Variables
      string         _filePath;  
      int32_t        _fd;
      uint32_t       _blockSize;
      uint32_t       _promSize;
      bool           _en;
      struct TemInfo _info;

      //! Write the .mcs file to the PROM
      bool unbufferedWriteBootProm ( );       

      //! Write the .mcs file to the PROM
      bool bufferedWriteBootProm ( );       
      
      //! Erase Command
      void eraseCommand(uint32_t address);
      
      //! Program Command
      void programCommand(uint32_t address, uint16_t data);
      
      //! Buffered Program Command
      void bufferedProgramCommand(uint32_t *address, uint16_t *data, uint16_t size);

      //! Read FLASH memory Command
      uint16_t readWordCommand(uint32_t address);

      //! Generic FLASH write Command
      void writeToFlash(uint32_t address, uint16_t cmd, uint16_t data);

      //! Generic FLASH read Command
      uint16_t readFlash(uint32_t address, uint16_t cmd);        
};
#endif
