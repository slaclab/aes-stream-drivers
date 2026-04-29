/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    Stub replacement for NVIDIA's kernel-open/nvidia/nv-p2p.h. Provides the
 *    minimum struct layouts and function prototypes required by the datadev
 *    driver's gpu_async.c when compiled with DATA_GPU. Loaded via
 *    nvidia_p2p_stub.ko for CI testing without an NVIDIA GPU.
 *
 *    Field set is intentionally minimal — only fields that gpu_async.c
 *    reads or writes are present.
 * ----------------------------------------------------------------------------
**/

#ifndef __NV_P2P_H__
#define __NV_P2P_H__

#include <linux/types.h>
#include <linux/pci.h>

/* Version constants — opaque; real NVIDIA header also uses magic values */
#define NVIDIA_P2P_PAGE_TABLE_VERSION 0x00010002
#define NVIDIA_P2P_DMA_MAPPING_VERSION 0x00020003

enum nvidia_p2p_page_size_type {
   NVIDIA_P2P_PAGE_SIZE_4KB  = 0,
   NVIDIA_P2P_PAGE_SIZE_64KB = 1,
   NVIDIA_P2P_PAGE_SIZE_128KB = 2,
   NVIDIA_P2P_PAGE_SIZE_COUNT
};

struct nvidia_p2p_page;

typedef struct nvidia_p2p_page_table {
   uint32_t version;
   uint32_t page_size;
   struct nvidia_p2p_page **pages;
   uint32_t entries;
   uint8_t *gpu_uuid;
   uint32_t flags;
} nvidia_p2p_page_table_t;

typedef struct nvidia_p2p_dma_mapping {
   uint32_t version;
   enum nvidia_p2p_page_size_type page_size_type;
   uint32_t entries;
   uint64_t *dma_addresses;
   void *private;
   struct pci_dev *pci_dev;
} nvidia_p2p_dma_mapping_t;

/* Function prototypes — signatures must exactly match NVIDIA's upstream
 * kernel-open/nvidia/nv-p2p.h so the datadev driver compiles unchanged. */

int nvidia_p2p_get_pages(uint64_t p2p_token, uint32_t va_space,
                         uint64_t virtual_address, uint64_t length,
                         struct nvidia_p2p_page_table **page_table,
                         void (*free_callback)(void *data), void *data);

int nvidia_p2p_put_pages(uint64_t p2p_token, uint32_t va_space,
                         uint64_t virtual_address,
                         struct nvidia_p2p_page_table *page_table);

int nvidia_p2p_dma_map_pages(struct pci_dev *peer,
                             struct nvidia_p2p_page_table *page_table,
                             struct nvidia_p2p_dma_mapping **dma_mapping);

int nvidia_p2p_dma_unmap_pages(struct pci_dev *peer,
                               struct nvidia_p2p_page_table *page_table,
                               struct nvidia_p2p_dma_mapping *dma_mapping);

int nvidia_p2p_free_page_table(struct nvidia_p2p_page_table *page_table);

#endif  /* __NV_P2P_H__ */
