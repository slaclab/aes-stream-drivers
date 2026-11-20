Route Multiple DMA Channels
===========================

This guide covers subscribing a file descriptor to multiple DMA receive
channels and sending data to a specific channel.

.. note::

   For background on how channels work with AXI stream TDEST bits, see
   :doc:`../explanation/axi-stream-protocol`.

Overview
--------

- The driver supports up to ``DMA_MAX_DEST`` (4096) channels
- Each ``open()`` call creates an independent client with its own channel mask
- A client only receives frames whose TDEST matches its mask
- Each channel can be claimed by only one open descriptor at a time; the mask
  call returns ``-1`` if any requested channel is already claimed
- Once a descriptor holds a non-empty mask, further mask calls on it fail;
  to change the mask, close and reopen the device

Subscribe to Channels 0-31
--------------------------

``dmaSetMask`` takes a 32-bit bitmask in which bit *N* selects channel *N*,
so it can only address channels 0 through 31:

.. code-block:: c

   #include <DmaDriver.h>
   #include <fcntl.h>

   int fd = open("/dev/datadev_0", O_RDWR);
   dmaSetMask(fd, 0x1);   /* bit 0: subscribe to channel 0 */

Subscribe to Any Set of Channels
--------------------------------

Use the byte-mask API to subscribe to any combination of the 4096 channels:

.. code-block:: c

   #include <DmaDriver.h>
   #include <fcntl.h>
   #include <string.h>

   int fd = open("/dev/datadev_0", O_RDWR);

   uint8_t mask[DMA_MASK_SIZE];   /* DMA_MASK_SIZE = 512 bytes */
   dmaInitMaskBytes(mask);         /* zero the mask */
   dmaAddMaskBytes(mask, 0);       /* subscribe to channel 0 */
   dmaAddMaskBytes(mask, 1);       /* subscribe to channel 1 */
   dmaAddMaskBytes(mask, 5);       /* subscribe to channel 5 */
   dmaSetMaskBytes(fd, mask);      /* apply the mask via ioctl */

Send to a Specific Channel
--------------------------

.. code-block:: c

   char buf[4096];
   uint32_t dest = 3;   /* target channel */
   ssize_t ret = dmaWrite(fd, buf, sizeof(buf), 0 /* flags */, dest);

Receive and Return Buffers
--------------------------

.. warning::

   Every ``dmaReadIndex`` call must be paired with ``dmaRetIndex``. If buffers
   are not returned, the receive pool (``cfgRxCount`` buffers) will drain and
   all future reads will block permanently.

.. code-block:: c

   uint32_t index, flags, error, dest;
   ssize_t ret = dmaReadIndex(fd, &index, &flags, &error, &dest);
   if (ret > 0) {
       /* process data at dmaBuffers[index] (from dmaMapDma) */
       dmaRetIndex(fd, index);   /* MUST call this to return the buffer */
   }

Multiple Clients on One Device
------------------------------

Open the same device file multiple times to create independent clients:

.. code-block:: c

   int fd_a = open("/dev/datadev_0", O_RDWR);
   int fd_b = open("/dev/datadev_0", O_RDWR);

   uint8_t mask_a[DMA_MASK_SIZE], mask_b[DMA_MASK_SIZE];
   dmaInitMaskBytes(mask_a); dmaAddMaskBytes(mask_a, 0);
   dmaInitMaskBytes(mask_b); dmaAddMaskBytes(mask_b, 1);
   dmaSetMaskBytes(fd_a, mask_a);   /* fd_a receives channel 0 */
   dmaSetMaskBytes(fd_b, mask_b);   /* fd_b receives channel 1 */

Each ``open(2)`` creates an independent ``DmaDesc`` with its own channel mask
and receive queue.
