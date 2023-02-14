#!/usr/bin/bash

# Location of the driver
DRV="$PWD/../driver/datadev.ko"

# Size of the buffer, 128K default
BUFF_SIZE=131072

# Number of receive buffers
BUFF_RX=1024

# Number of transmit buffers
BUFF_TX=1024

# Buffer Mode
# BUFF_COHERENT  1
# BUFF_STREAM    2
# BUFF_ARM_ACP   4
BUFF_MODE=1

# Receive Continue Enable
RX_CONT=1

# IRQ Holdoiff Value
RX_CONT=10000

# IRQ Disable, poll thread CPU
IRQ_DIS=0

# Buffer Group Threshold 0
BG_THOLD0=0

# Buffer Group Threshold 1
BG_THOLD1=0

# Buffer Group Threshold 2
BG_THOLD2=0

# Buffer Group Threshold 3
BG_THOLD3=0

# Buffer Group Threshold 4
BG_THOLD4=0

# Buffer Group Threshold 5
BG_THOLD5=0

# Buffer Group Threshold 6
BG_THOLD6=0

# Buffer Group Threshold 7
BG_THOLD7=0

# Attempt to load the driver
/usr/bin/insmod $DRV cfgSize=$BUFF_SIZE \
                     cfgRxCount=$BUFF_RX \
                     cfgTxCount=$BUFF_TX \
                     cfgMode=$BUFF_MODE \
                     cfgCont=$RX_CONT \
                     cfgIrqDis=$IRQ_DIS \
                     cfgBgThold0=$BG_THOLD0 \
                     cfgBgThold1=$BG_THOLD1 \
                     cfgBgThold2=$BG_THOLD2 \
                     cfgBgThold3=$BG_THOLD3 \
                     cfgBgThold4=$BG_THOLD4 \
                     cfgBgThold5=$BG_THOLD5 \
                     cfgBgThold6=$BG_THOLD6 \
                     cfgBgThold7=$BG_THOLD7

# Check results
if [ $? -eq 0 ]
then

   if [ -f "/dev/datadev_0" ]
   then
      echo "Found /dev/datadev_0"
   else
      echo "ERROR: Failed to find any hardware devices!"
      exit 1
   fi
else
   echo "ERROR: Driver failed to load!"
   exit 1
fi

