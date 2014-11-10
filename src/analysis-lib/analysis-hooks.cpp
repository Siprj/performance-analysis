/*
 * Copyright (c) 2014, Jan Šipr
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of  nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "utils.h"
#include "atomic-cache.h"
#include "time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Pointer size is stored at beginig of loging file
// Chunk is composed from: chunk type (1B), time (16), function address
//      (pointer size), address of caller which called function (pointer size)
#define DATA_CHUNK_SIZE 1 + 16 + sizeof(void*) + sizeof(void*)

enum {
    ENTER_TYPE = 1,
    EXIT_TYPE = 2
};

typedef struct __attribute__ ((packed))
{
    uint8_t type;
    struct timespec time;
    void *function;
    void *caller;
}DataChunk;

extern "C"
{

void __cyg_profile_func_enter(void *function,  void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *function, void *caller) __attribute__((no_instrument_function));

} // extern "C"

void __attribute__ ((constructor)) init_analysis(void)
{
    int fileDescriptor;
    if ((fileDescriptor = open("test.bin", O_WRONLY | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
    {
        perror("Cannot open output file\n");
        exit(1);
    }
    AtomicCache::initAtomicCache(fileDescriptor, DATA_CHUNK_SIZE);

    // Store pointer size
    uint8_t pointerSize = (uint8_t)sizeof(void*);
    AtomicCache::storeDataChunk(&pointerSize, 1);
}
 
void __attribute__ ((destructor)) deinit_analysis(void)
{
    AtomicCache::deinitAtomicCache();
}

extern "C"
{

void __cyg_profile_func_enter(void *function,  void *caller)
{
    struct timespec time;
    ON_FAILURE(clock_gettime(CLOCK_MONOTONIC, &time))
    {
        printf("get time error error: %s", strerror(errno));
    }
    // Create ENTER data chunk
    DataChunk chunk = {ENTER_TYPE, time, function, caller };

    // Store data chunk
    AtomicCache::storeDataChunk((uint8_t*)(&chunk), sizeof(chunk));
}
 
void __cyg_profile_func_exit(void *function, void *caller)
{
    struct timespec time;
    ON_FAILURE(clock_gettime(CLOCK_MONOTONIC, &time))
    {
        printf("get time error error: %s", strerror(errno));
    }
    // Create EXIT data chunk
    DataChunk chunk = {EXIT_TYPE, time, function, caller };

    // Store data chunk
    AtomicCache::storeDataChunk((uint8_t*)(&chunk), sizeof(chunk));
}

} // extern "C"
