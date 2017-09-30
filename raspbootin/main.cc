/* main.cc - the entry point for the kernel */
/* Copyright (C) 2013 Goswin von Brederlow <goswin-v-b@web.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdint.h>
#include <archinfo.h>
#include <uart.h>
#include <kprintf.h>
#include <atag.h>

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC

#include "../miniz/miniz.h"
#include "../miniz/miniz_common.h"
#include "../miniz/miniz_tdef.h"

#ifdef getc
#undef getc
#endif


extern "C" {
    // kernel_main gets called from boot.S. Declaring it extern "C" avoid
    // having to deal with the C++ name mangling.
    void kernel_main(uint32_t r0, uint32_t r1, const Header *atags);
}


#define LOADER_ADDR 0x2000000
#define MAX_SIZE     0x200000

const char hello[] = "\r\nRaspbootin V1.1\r\n";
const char halting[] = "\r\n*** system halting ***";

typedef void (*entry_fn)(uint32_t r0, uint32_t r1, const Header *atags);

static constexpr ArchInfo arch_infos[ArchInfo::NUM_ARCH_INFOS] = {
    ArchInfo("Raspberry Pi b", 0x20000000, 16, 1),
    ArchInfo("Raspberry Pi b+", 0x20000000, 47, 0),
    ArchInfo("Raspberry Pi b 2", 0x3F000000, 47, 0),
};

const ArchInfo *arch_info;

// clib functions needed by miniz
void *memset(void *s, int c, size_t n) {
    char* ptr = (char*)s;
    for(int i = 0; i < n; ++i) {
        ptr[i] = c;
    }

    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char* pdest = (char*)dest;
    const char* psrc = (char*)src;

    for(int i = 0; i < n; ++i) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *malloc(size_t size) {
    static char* base = (char*)(0x8000+MAX_SIZE*2);

    void* retval = base;
    base += size;

    return retval;
}

void free(void *ptr) { (void)ptr; }

void *calloc(size_t nmemb, size_t size) {
    void* ptr = malloc(nmemb*size);
    memset(ptr, 0, nmemb*size);

    return ptr;
}

void *realloc(void *ptr, size_t size) {
    void* retval = malloc(size);

    memcpy(retval, ptr, size);

    return retval;
}

void __assert_func (const char *, int, const char *, const char *) {
    while(true);
}

const char *find(const char *str, const char *token) {
    while(*str) {
        const char *p = str;
        const char *q = token;
        // as long as str matches token
        while(*p && *p == *q) {
            // keep comparing
            ++p;
            ++q;
        }
        if (*q == 0) return str; // found token
        // token not found, try again
        ++str;
    }

    // end of string, nothing found
    return NULL;
}

// kernel main function, it all begins here
void kernel_main(uint32_t r0, uint32_t r1, const Header *atags) {
    // Fixgure out what kind of Raspberry we are booting on
    // default to basic Raspberry Pi
    arch_info = &arch_infos[ArchInfo::RPI];
    const Cmdline *cmdline = atags->find<Cmdline>();
    if (find(cmdline->cmdline, "bcm2708.disk_led_gpio=47")) {
        arch_info = &arch_infos[ArchInfo::RPIplus];
    }
    if (find(cmdline->cmdline, "bcm2709.disk_led_gpio=47")) {
        arch_info = &arch_infos[ArchInfo::RPI2];
    }
    
    UART::init();
again:
    kprintf(hello);
    kprintf("######################################################################\n");
    kprintf("R0 = %#010lx, R1 = %#010lx, ATAGs @ %p\n", r0, r1, atags);
    atags->print_all();
    kprintf("Detected '%s'\n", arch_info->model);
    kprintf("######################################################################\n");

    // request kernel by sending 3 breaks
    UART::puts("\x03\x03\x03");
    // get kernel size
    uint32_t size = UART::getc();
    size |= UART::getc() << 8;
    size |= UART::getc() << 16;
    size |= UART::getc() << 24;

    if (0x8000 + size > LOADER_ADDR) {
        UART::puts("SE");
        goto again;
    } else {
        UART::puts("OK");
    }

    // get compressed kernel
    uint8_t *compressed = (uint8_t*)(0x8000+MAX_SIZE);
    for (int i = 0; i < size; ++i) {
        compressed[i] = UART::getc();
    }


    kprintf("decompressing...\r\n");

    // decompress
    uint8_t *kernel = (uint8_t*)0x8000;
    unsigned long uncompressedSize = MAX_SIZE;
    int cmp_status = uncompress(kernel, &uncompressedSize, compressed, size);
    if (cmp_status == Z_OK) {
        UART::puts("decompressed OK...\r\n");
    } else {
        UART::puts("decompressed failed...\r\n");
    }

    unsigned long crc = crc32(0, kernel, uncompressedSize);
    kprintf("CRC: 0x%08lx\n", crc);

    // Kernel is loaded at 0x8000, call it via function pointer
    UART::puts("booting...");
    entry_fn fn = (entry_fn)0x8000;
    fn(r0, r1, atags);

    // fn() should never return. But it might, so make sure we catch it.
    // Wait a bit
    for(volatile int i = 0; i < 10000000; ++i) { }

    // Say goodbye and return to boot.S to halt.
    UART::puts(halting);
}

