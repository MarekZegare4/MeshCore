#include "DeviceDiag.h"

#if defined(NRF52_PLATFORM)
#include "FreeRTOS.h"
#include "task.h"
#include <malloc.h>

// Heap region size from the linker symbols the core's new.cpp uses for sbrk
// (framework-arduinoadafruitnrf52/cores/nRF5/new.cpp). "Used" comes from
// newlib's mallinfo().uordblks (bytes actually outstanding) rather than
// sbrk(0) - __HeapBase: sbrk only ever grows, so a high-water mark there
// counts freed-and-recycled blocks as permanently "used" and free heap looks
// far lower than it really is.
extern unsigned char __HeapBase[];
extern unsigned char __HeapLimit[];

void DeviceDiag::getHeapStats(uint32_t& free_bytes, uint32_t& total_bytes) {
  total_bytes = (uint32_t)(__HeapLimit - __HeapBase);
  uint32_t used = (uint32_t)mallinfo().uordblks;
  free_bytes = (used < total_bytes) ? (total_bytes - used) : 0;
}

uint32_t DeviceDiag::getStackFreeBytes() {
  return (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
}

#elif defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

// MALLOC_CAP_8BIT: the general-purpose byte-addressable heap (what `new`/
// malloc() actually draw from) -- excludes DMA-only/IRAM regions a plain
// allocation could never land in, so this matches what's really available to
// the rest of the firmware rather than overstating it with capability pools
// nothing here can use.
void DeviceDiag::getHeapStats(uint32_t& free_bytes, uint32_t& total_bytes) {
  free_bytes  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
  total_bytes = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_8BIT);
}

uint32_t DeviceDiag::getStackFreeBytes() {
  return (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
}

#else

void DeviceDiag::getHeapStats(uint32_t& free_bytes, uint32_t& total_bytes) {
  free_bytes = 0; total_bytes = 0;
}
uint32_t DeviceDiag::getStackFreeBytes() { return 0; }

#endif
