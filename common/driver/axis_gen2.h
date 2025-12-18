/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This header file defines the interfaces and data structures for the Axis
 *    Gen2 DMA driver. It includes function prototypes for device initialization,
 *    data transmission, and reception management, along with definitions for
 *    handling device-specific commands and interrupts. Designed for
 *    high-performance data movement, this API facilitates efficient
 *    communication between the host and Axis Gen2 hardware.
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/

#ifndef __AXIS_GEN2_H__
#define __AXIS_GEN2_H__

#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#define AXIS2_RING_ACP 0x10
#define BUFF_LIST_SIZE 1000

/**
 * struct AxisG2Reg - AXIS Gen2 Register Map.
 * @enableVer: Version and enable register (0x0000).
 * @intEnable: Interrupt enable register (0x0004).
 * @contEnable: Continuous operation enable register (0x0008).
 * @dropEnable: Drop packet enable register (0x000C).
 * @wrBaseAddrLow: Write base address low register (0x0010).
 * @wrBaseAddrHigh: Write base address high register (0x0014).
 * @rdBaseAddrLow: Read base address low register (0x0018).
 * @rdBaseAddrHigh: Read base address high register (0x001C).
 * @fifoReset: FIFO reset register (0x0020).
 * @spareA: Spare register A (0x0024).
 * @maxSize: Maximum transfer size register (0x0028).
 * @online: Online status register (0x002C).
 * @acknowledge: Interrupt acknowledge register (0x0030).
 * @channelCount: Number of channels register (0x0034).
 * @addrWidth: Address width register (0x0038).
 * @cacheConfig: Cache configuration register (0x003C).
 * @readFifoA: Read FIFO A register (0x0040).
 * @readFifoB: Read FIFO B register (0x0044).
 * @writeFifoA: Write FIFO A register (0x0048).
 * @intAckAndEnable: Interrupt acknowledge and enable register (0x004C).
 * @intReqCount: Interrupt request count register (0x0050).
 * @hwWrIndex: Hardware write index register (0x0054).
 * @hwRdIndex: Hardware read index register (0x0058).
 * @wrReqMissed: Write request missed register (0x005C).
 * @readFifoC: Read FIFO C register (0x0060).
 * @readFifoD: Read FIFO D register (0x0064).
 * @spareB: Spare registers B (0x0068 - 0x006C).
 * @writeFifoB: Write FIFO B register (0x0070).
 * @spareC: Spare registers C (0x0074 - 0x007C).
 * @forceInt: Force interrupt register (0x0080).
 * @irqHoldOff: IRQ hold-off time register (0x0084).
 * @spareD: Spare registers D (0x0088 - 0x008C).
 * @bgThold: Background threshold registers (0x0090 - 0x00AC).
 * @bgCount: Background count registers (0x00B0 - 0x00CC).
 * @spareE: Spare registers E (0x00D0 - 0x3FFC).
 * @dmaAddr: DMA address array (0x4000 - 0x7FFC).
 *
 * This structure maps the AXIS Gen2 registers. It includes control, status,
 * configuration, and data buffer registers. The map is designed for efficient
 * hardware access to perform DMA operations.
 */
struct AxisG2Reg {
   uint32_t enableVer;        // 0x0000
   uint32_t intEnable;        // 0x0004
   uint32_t contEnable;       // 0x0008
   uint32_t dropEnable;       // 0x000C
   uint32_t wrBaseAddrLow;    // 0x0010
   uint32_t wrBaseAddrHigh;   // 0x0014
   uint32_t rdBaseAddrLow;    // 0x0018
   uint32_t rdBaseAddrHigh;   // 0x001C
   uint32_t fifoReset;        // 0x0020
   uint32_t spareA;           // 0x0024
   uint32_t maxSize;          // 0x0028
   uint32_t online;           // 0x002C
   uint32_t acknowledge;      // 0x0030
   uint32_t channelCount;     // 0x0034
   uint32_t addrWidth;        // 0x0038
   uint32_t cacheConfig;      // 0x003C
   uint32_t readFifoA;        // 0x0040
   uint32_t readFifoB;        // 0x0044
   uint32_t writeFifoA;       // 0x0048
   uint32_t intAckAndEnable;  // 0x004C
   uint32_t intReqCount;      // 0x0050
   uint32_t hwWrIndex;        // 0x0054
   uint32_t hwRdIndex;        // 0x0058
   uint32_t wrReqMissed;      // 0x005C
   uint32_t readFifoC;        // 0x0060
   uint32_t readFifoD;        // 0x0064
   uint32_t spareB[2];        // 0x0068 - 0x006C
   uint32_t writeFifoB;       // 0x0070
   uint32_t spareC[3];        // 0x0074 - 0x007C
   uint32_t forceInt;         // 0x0080
   uint32_t irqHoldOff;       // 0x0084
   uint32_t timeout;          // 0x0088
   uint32_t spareD;           // 0x008C
   uint32_t bgThold[8];       // 0x0090 - 0x00AC
   uint32_t bgCount[8];       // 0x00B0 - 0x00CC
   uint32_t spareE[4044];     // 0x00D0 - 0x3FFC
   uint32_t dmaAddr[4096];    // 0x4000 - 0x7FFC
};

/**
 * struct AxisG2Return - Represents the return structure for AXIS Gen2.
 *
 * This structure is used to store the result of an AXIS Gen2 operation, providing
 * various details about the transaction including its size, result, and user-defined
 * flags. It's part of the data handling in DMA transactions, capturing the essence
 * of a completed operation's outcome.
 *
 * @index:  The index of the operation, typically used for identifying the
 *          transaction in a sequence of operations.
 * @size:   The size of the data transferred in this operation, in bytes.
 * @result: The result of the operation, where a specific value usually indicates
 *          success or a certain type of failure.
 * @fuser:  The first user-defined value, which can be used for tagging the
 *          operation with custom information at the start.
 * @luser:  The last user-defined value, which can be used for tagging the
 *          operation with custom information at the end.
 * @dest:   The destination identifier, useful for routing the result to the
 *          correct handler or buffer.
 * @cont:   A flag indicating whether the operation is continuous or a single shot.
 *          This can be used to control the flow of data processing.
 * @id:     An identifier for the operation, providing a unique tag for tracking
 *          and referencing purposes.
 * @timeout: When set, the transaction timed out.
 */
struct AxisG2Return {
   uint32_t index;    // Index of the operation
   uint32_t size;     // Size of the data transferred
   uint8_t  result;   // Result of the operation
   uint8_t  fuser;    // First user-defined value
   uint8_t  luser;    // Last user-defined value
   uint16_t dest;     // Destination identifier
   uint8_t  cont;     // Continuous operation flag
   uint8_t  id;       // Operation identifier
   uint8_t  timeout;  // Timeout flag
};

/**
 * struct AxisG2Data - Structure to manage AXIS Gen2 DMA data.
 * @dev: Pointer to the associated DmaDevice structure.
 * @desc128En: Indicates if 128-bit descriptors are enabled.
 * @readAddr: Pointer to the base address for DMA read operations.
 * @readHandle: DMA handle for the read operations.
 * @readIndex: Current index in the read buffer.
 * @writeAddr: Pointer to the base address for DMA write operations.
 * @writeHandle: DMA handle for the write operations.
 * @writeIndex: Current index in the write buffer.
 * @addrCount: Number of addresses for DMA operations.
 * @missedIrq: Counter for missed IRQs.
 * @hwWrBuffCnt: Hardware write buffer count.
 * @hwRdBuffCnt: Hardware read buffer count.
 * @wrQueue: Write queue for managing DMA write requests.
 * @rdQueue: Read queue for managing DMA read requests.
 * @contCount: Counter for continuous operations.
 * @bgEnable: Flag to enable background operations.
 * @wqEnable: Flag to enable workqueue operations.
 * @wq: Pointer to the workqueue structure.
 * @dlyWork: Delayed work structure for scheduling tasks.
 * @irqWork: Work structure for IRQ handling.
 * @buffList: Pointer to a list of DmaBuffer pointers.
 *
 * This structure is used by the AXIS Gen2 DMA driver to manage data related
 * to DMA operations, including addressing, buffers, and hardware counters.
 */
struct AxisG2Data {
   struct DmaDevice *dev;

   uint32_t    desc128En;

   uint32_t  * readAddr;
   dma_addr_t  readHandle;
   uint32_t    readIndex;

   uint32_t  * writeAddr;
   dma_addr_t  writeHandle;
   uint32_t    writeIndex;

   uint32_t    addrCount;
   uint32_t    missedIrq;

   uint32_t    hwWrBuffCnt;
   uint32_t    hwRdBuffCnt;

   struct DmaQueue wrQueue;
   struct DmaQueue rdQueue;

   uint32_t    contCount;

   uint32_t    bgEnable;
   uint32_t    wqEnable;

   uint32_t    timeoutAvail;

   struct workqueue_struct *wq;
   struct delayed_work dlyWork;
   struct work_struct  irqWork;

   struct DmaBuffer  ** buffList;
};

// Function prototypes
inline uint8_t AxisG2_MapReturn(struct DmaDevice *dev, struct AxisG2Return *ret, uint32_t desc128En, uint32_t index, uint32_t *ring);
inline void AxisG2_WriteFree(struct DmaBuffer *buff, __iomem struct AxisG2Reg *reg, uint32_t desc128En);
inline void AxisG2_WriteTx(struct DmaBuffer *buff, __iomem struct AxisG2Reg *reg, uint32_t desc128En);
uint32_t AxisG2_Process(struct DmaDevice * dev, __iomem struct AxisG2Reg *reg, struct AxisG2Data *hwData);
irqreturn_t AxisG2_Irq(int irq, void *dev_id);
void AxisG2_Init(struct DmaDevice *dev);
void AxisG2_Enable(struct DmaDevice *dev);
void AxisG2_Clear(struct DmaDevice *dev);
void AxisG2_IrqEnable(struct DmaDevice *dev, int en);
void AxisG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);
int32_t AxisG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
void AxisG2_SeqShow(struct seq_file *s, struct DmaDevice *dev);
extern struct hardware_functions AxisG2_functions;
void AxisG2_WqTask_IrqForce(struct work_struct *work);
void AxisG2_WqTask_Poll(struct work_struct *work);
void AxisG2_WqTask_Service(struct work_struct *work);

#endif  // __AXIS_GEN2_H__
