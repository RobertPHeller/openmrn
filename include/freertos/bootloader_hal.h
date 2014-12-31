/** \copyright
 * Copyright (c) 2014, Balazs Racz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file bootloader_hal.hxx
 *
 * Hardware support functions for bootloader operation.
 *
 * @author Balazs Racz
 * @date 8 Dec 2014
 */

#include <stdint.h>
#include "can_frame.h"


#ifdef __cplusplus
extern "C" {
#else
typedef unsigned bool;
#endif

// Number of 32-bit words in one checksum data.
#define CHECKSUM_COUNT 4

/** Static definitions of the application. These will be flashed with the
 * application, and contain checksum on whether the application is valid or
 * not. */
struct app_header {
    /** The total length of the application binary. The start of the
     * application binary is always at flash_min of get_flash_info. This number
     * tells how many bytes need to be checksummed. */
    uint32_t app_size;
    /** Checksum data for the application info. The checksumming algorithm is
     * not fixed -- the HAL should provide it in case there is hardware CRC
     * support. Unused entries should be filled with zeros.
     *
     * checksum_pre: is the checksum of the bytes between application_start and
     * the app_header.
     *
     * checksum_post: is the checksum of the bytes between the checksum header
     * and the application end. */
    uint32_t checksum_pre[CHECKSUM_COUNT];
    uint32_t checksum_post[CHECKSUM_COUNT];
};

enum BootloaderLed {
  LED_ACTIVE = 1,
  LED_WRITING = 2,
  LED_IDENT = 4,
};

/** Initializes the hardware to a safe state of the outputs. This function may
 *  not assume anything about previous hardware state. Also the memory layout
 *  is not yet initialized. (Do not use any static objects, and the DATA
 *  segment.)
 *
 *  This function should also disable interrupts. */
extern void bootloader_hw_set_to_safe(void);

/** Called after hw_set_to_safe and after the bss and data segments are
 *  initialized. Initializes the processor state, CAN hardware etc. */
extern void bootloader_hw_init(void);

/** @Returns true if the hardware state requests entry to the bootloader. This
 *  will typically read a GPIO pin for a bootloader switch. This function will
 *  run after hw_init. */
extern bool request_bootloader(void);

/** Enters the application. Never returns. */
extern void application_entry(void);

/** Resets the microcontroller. Never returns. */
extern void bootloader_reboot(void);

/** Sets the LEDs indicated by @param mask to the value indicated by @param
 *  value. */
extern void bootloader_led(uint32_t mask, uint32_t value);

/** Checks if there is an incoming CAN frame from the hardware.
 *
 * @param frame will be loaded with the incoming frame.
 *
 * @returns true if a frame has arrived, false if no frame was loaded into
 * frame. */
extern bool read_can_frame(struct can_frame *frame);

#ifdef __cplusplus

/** Tries to send a can frame.
 *
 * @param frame is the frame to send.
 *
 * @returns true if the frame was sent, false if the hardware buffer was busy
 * and the operation should be re-tried later. */
extern bool try_send_can_frame(const struct can_frame &frame);

#endif


/** Returns the boundaries of the user flash.
 *
 * @param flash_min is set to the pointer of the first valid byte to be flashed.
 * @param flash_max is set to the pointer of the last valid byte to be flashed.
 * @param app_header is set to the pointer in the application flash where the
 * application header is located.
 */
extern void get_flash_boundaries(const void **flash_min, const void **flash_max,
    const struct app_header **app_header);

/** Rounds a flash address into a flash page.
 *
 * @param address is the address for which the page information is queried.
 * @param page_start will be set to the first byte of that page.
 * @param page_length_bytes is set to the number of bytes in that flash page.
 *
 * In other words, *page_start <= address < (*page_start + *page_length_bytes).
 */
extern void get_flash_page_info(
    const void *address, const void **page_start, uint32_t *page_length_bytes);

/** Erases the flash page at a specific address. Blocks the caller until the
 * flash erase is successful. (Microcontrollers often cannot execute code while
 * the flash is being written or erased, so a polling mechanism would not help
 * here too much.)
 *
 * @param address is the start address of a valid page, as returned by
 * get_flash_page_info.
 */
extern void erase_flash_page(const void *address);

/** Writes data to the flash.
 *
 * @param address is the location to write data to. Aligned to 4 bytes.
 * @param data is the buffer to write data from.
 * @param size_bytes is the total number of bytes to write. Has to be a
 * multiple of 4.
 */
extern void write_flash(
    const void *address, const void *data, uint32_t size_bytes);

/** Computes checksum over a block of data.
 *
 * @param data is the data to be checksummed.
 * @param size is the number of bytes to checksum.
 * @param checksum is a 16-byte array which will be filled with the checksum
 * data. Unused entries have to be zeroed. */
extern void checksum_data(const void* data, uint32_t size, uint32_t* checksum);

/** Suggests an NMRAnet CAN alias for use. If the running application has saved
 *  the last used alias, this function returns it. */
extern uint16_t nmranet_alias(void);

/** @returns the NMRAnet NodeID for this hardware node. */
extern uint64_t nmranet_nodeid(void);

#ifdef __cplusplus
}
#endif
