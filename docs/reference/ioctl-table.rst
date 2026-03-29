Ioctl Command Code Reference
=============================

All ioctl command codes understood by the ``datadev`` driver and associated
headers. Pass these codes as the second argument to ``ioctl(fd, cmd, arg)``.

.. note::

   The ``DMA_Ret_Index`` command encodes a count in the upper 16 bits of
   the command code. To return *N* buffers, use
   ``DMA_Ret_Index | ((N << 16) & 0xFFFF0000)``. For a single index,
   use ``DMA_Ret_Index | 0x10000``.


DmaDriver.h Commands
--------------------

.. list-table::
   :header-rows: 1
   :widths: 32 10 28 10 20

   * - Macro
     - Value
     - Argument
     - Direction
     - Wrapper Function
   * - ``DMA_Get_Buff_Count``
     - ``0x1001``
     - none (pass 0)
     - read
     - :c:func:`dmaGetBuffCount`
   * - ``DMA_Get_Buff_Size``
     - ``0x1002``
     - none (pass 0)
     - read
     - :c:func:`dmaGetBuffSize`
   * - ``DMA_Set_Debug``
     - ``0x1003``
     - ``uint32_t`` level
     - write
     - :c:func:`dmaSetDebug`
   * - ``DMA_Set_Mask``
     - ``0x1004``
     - ``uint32_t`` mask
     - write
     - :c:func:`dmaSetMask`
   * - ``DMA_Ret_Index``
     - ``0x1005``
     - ``uint32_t*`` index (count in upper 16 bits of cmd)
     - write
     - :c:func:`dmaRetIndex`, :c:func:`dmaRetIndexes`
   * - ``DMA_Get_Index``
     - ``0x1006``
     - none (pass 0)
     - read
     - :c:func:`dmaGetIndex`
   * - ``DMA_Read_Ready``
     - ``0x1007``
     - none (pass 0)
     - read
     - :c:func:`dmaReadReady`
   * - ``DMA_Set_MaskBytes``
     - ``0x1008``
     - ``uint8_t[512]`` mask
     - write
     - :c:func:`dmaSetMaskBytes`
   * - ``DMA_Get_Version``
     - ``0x1009``
     - none (pass 0)
     - read
     - :c:func:`dmaCheckVersion`, :c:func:`dmaGetApiVersion`
   * - ``DMA_Write_Register``
     - ``0x100A``
     - ``struct DmaRegisterData*``
     - write
     - :c:func:`dmaWriteRegister`
   * - ``DMA_Read_Register``
     - ``0x100B``
     - ``struct DmaRegisterData*``
     - read/write
     - :c:func:`dmaReadRegister`
   * - ``DMA_Get_RxBuff_Count``
     - ``0x100C``
     - none (pass 0)
     - read
     - :c:func:`dmaGetRxBuffCount`
   * - ``DMA_Get_TxBuff_Count``
     - ``0x100D``
     - none (pass 0)
     - read
     - :c:func:`dmaGetTxBuffCount`
   * - ``DMA_Get_TxBuffinUser_Count``
     - ``0x100F``
     - none (pass 0)
     - read
     - :c:func:`dmaGetTxBuffinUserCount`
   * - ``DMA_Get_TxBuffinHW_Count``
     - ``0x1010``
     - none (pass 0)
     - read
     - :c:func:`dmaGetTxBuffinHwCount`
   * - ``DMA_Get_TxBuffinPreHWQ_Count``
     - ``0x1011``
     - none (pass 0)
     - read
     - :c:func:`dmaGetTxBuffinPreHwQCount`
   * - ``DMA_Get_TxBuffinSWQ_Count``
     - ``0x1012``
     - none (pass 0)
     - read
     - :c:func:`dmaGetTxBuffinSwQCount`
   * - ``DMA_Get_TxBuffMiss_Count``
     - ``0x1013``
     - none (pass 0)
     - read
     - :c:func:`dmaGetTxBuffMissCount`
   * - ``DMA_Get_RxBuffinUser_Count``
     - ``0x1014``
     - none (pass 0)
     - read
     - :c:func:`dmaGetRxBuffinUserCount`
   * - ``DMA_Get_RxBuffinHW_Count``
     - ``0x1015``
     - none (pass 0)
     - read
     - :c:func:`dmaGetRxBuffinHwCount`
   * - ``DMA_Get_RxBuffinPreHWQ_Count``
     - ``0x1016``
     - none (pass 0)
     - read
     - :c:func:`dmaGetRxBuffinPreHwQCount`
   * - ``DMA_Get_RxBuffinSWQ_Count``
     - ``0x1017``
     - none (pass 0)
     - read
     - :c:func:`dmaGetRxBuffinSwQCount`
   * - ``DMA_Get_RxBuffMiss_Count``
     - ``0x1018``
     - none (pass 0)
     - read
     - :c:func:`dmaGetRxBuffMissCount`
   * - ``DMA_Get_GITV``
     - ``0x1019``
     - ``char[32]`` buffer
     - read
     - :c:func:`dmaGetGitVersion`


AxisDriver.h Commands
---------------------

.. list-table::
   :header-rows: 1
   :widths: 32 10 28 10 20

   * - Macro
     - Value
     - Argument
     - Direction
     - Wrapper Function
   * - ``AXIS_Read_Ack``
     - ``0x2001``
     - none (pass 0)
     - write
     - :c:func:`axisReadAck`
   * - ``AXIS_Write_ReqMissed``
     - ``0x2002``
     - none (pass 0)
     - write
     - :c:func:`axisWriteReqMissed`


AxiVersion.h Commands
---------------------

.. list-table::
   :header-rows: 1
   :widths: 32 10 28 10 20

   * - Macro
     - Value
     - Argument
     - Direction
     - Wrapper Function
   * - ``AVER_Get``
     - ``0x1200``
     - ``struct AxiVersion*``
     - read
     - ``axiVersionGet()``


GpuAsync.h Commands
-------------------

.. list-table::
   :header-rows: 1
   :widths: 32 10 28 10 20

   * - Macro
     - Value
     - Argument
     - Direction
     - Wrapper Function
   * - ``GPU_Add_Nvidia_Memory``
     - ``0x8002``
     - ``struct GpuNvidiaData*``
     - write
     - ``gpuAddNvidiaMemory()``
   * - ``GPU_Rem_Nvidia_Memory``
     - ``0x8003``
     - none (pass 0)
     - write
     - ``gpuRemNvidiaMemory()``
   * - ``GPU_Set_Write_Enable``
     - ``0x8004``
     - ``uint32_t*`` index
     - write
     - ``gpuSetWriteEn()``
   * - ``GPU_Is_Gpu_Async_Supp``
     - ``0x8005``
     - none (pass 0)
     - read
     - ``gpuIsGpuAsyncSupported()``
   * - ``GPU_Get_Gpu_Async_Ver``
     - ``0x8006``
     - none (pass 0)
     - read
     - ``gpuGetGpuAsyncVersion()``
   * - ``GPU_Get_Max_Buffers``
     - ``0x8007``
     - none (pass 0)
     - read
     - ``gpuGetMaxBuffers()``
