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

#include "atomic-cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <stdint.h>
#include <atomic>
#include <assert.h>

#include "atomic-cache-conf.h"

// Flag which lock store chung function to make it thread safe
std::atomic_flag storeChunkLock = ATOMIC_FLAG_INIT;
// "Atomic" cache for data chunks. The atomicity should speed up whole process
// of saving data.
uint8_t dataCache[NUMBER_OF_CACHES][CACHE_SIZE];
// Index to cache. Indicate which indexes was already acquired by any writer.
std::atomic<int> indexStoreing[NUMBER_OF_CACHES];
// Index to cache. Indicate which indexes was already written by any writer.
std::atomic<int> indexStored[NUMBER_OF_CACHES];
// This variable distinguish between caches.
std::atomic<int> cacheActualNumber;

// Maximal size of chunk wchi will be stored in chache as one entry
uint32_t maxDataChunkSize_g;
// Number of entryes which can by stored in chache. This value is cumpiuted
// from CACHE_SIZE and maxDataChunkSize_g.
// cacheIndexCount = (CACHE_SIZE / maxDataChunkSize_g + 4)
// The four in previous formula are 4 bytes of chunk size
uint32_t cacheIndexCount;

// File descriptor passed to us by init
int fileDescriptor;

// Function wich take number of cache and write it in to file. Also second
// parameter is top boundary of cache (number of entries which will be
// written).
namespace AtomicCache
{
void writeCache(int cacheNumber, int numberOfEntries);
} // namespace AtomicCache


// Initialize data chunk storage.
// Find if the output path is presented inside at environment variables
void AtomicCache::initAtomicCache(int writeFileDescriptor,
                                  uint32_t maxDataChunkSize)
{
    assert(writeFileDescriptor > 0);
    assert(maxDataChunkSize > 0);

    fileDescriptor = writeFileDescriptor;
    maxDataChunkSize_g = maxDataChunkSize;
    cacheIndexCount = (CACHE_SIZE / maxDataChunkSize_g + 4);

    // Clear cache
    memset(&dataCache, 0, CACHE_SIZE*NUMBER_OF_CACHES);

    // Init cache variables
    for(int i = 0; i < NUMBER_OF_CACHES; i++)
    {
        indexStoreing[i] = 0;
        indexStored[i] = 0;
    }
    cacheActualNumber = 0;
}


void AtomicCache::deinitAtomicCache()
{
    printf("deinit\n");
    // Write rest of data in to file
    for(int i = 0; i < NUMBER_OF_CACHES; i++)
    {
        printf("for\n");
        if(indexStored[i].load() > 0)
        {
            printf("write cache\n");
            writeCache(i,indexStored[i].load());
        }
    }
}


void AtomicCache::writeCache(int cacheNumber, int numberOfEntries)
{
    // Lock until all data are written
    while (storeChunkLock.test_and_set())
    {
        fprintf(stderr, "CACHE IS FULL!!!\n");
    }
    // Go through cache and identify all data chunks. Write them with the
    // correct size.
    for(int i = 0; i < numberOfEntries; i++)
    {
        int realCacheIndex =
                i * (maxDataChunkSize_g + sizeof(uint32_t));
        int *chunkSizePointer = (int*)&dataCache[cacheNumber][realCacheIndex];
        int written = write(fileDescriptor,
            &dataCache[cacheNumber][realCacheIndex+sizeof(uint32_t)],
            *chunkSizePointer);
        (void)written;
    }
    // Unlock writeing
    storeChunkLock.clear();
}


// Store data chunk generated by memory hooks
void AtomicCache::storeDataChunk(uint8_t *dataChunk, uint32_t chunkSize)
{
    assert(chunkSize > 0);
    assert(chunkSize <= maxDataChunkSize_g);
    while(true) // infinite loop until passed chunk is stored in chache
    {
        int currentCache = cacheActualNumber.load();
        // MappedIndexInsideCache is index of cache entry. Cache entry is
        // small space inside cache and his size is maxDataChunkSize_g + 4
        //
        // +-----------------------------------------------------------+
        // |                        Byte array                         |
        // +----+----+----+----+----+----+----+----+----+----+----+----+
        // | 1B | 1B | 1B | 1B | 1B | 1B | 1B | 1B | 1B | 1B | 1B | 1B |
        // +-------------------+-------------------+-------------------+
        // |   cache entry 1   |   cache entry 2   |   cache entry 3   |
        // +--------------+----+--------------+----+--------------+----+
        // | stored data  |    | stored data  |    | stored data  |    |
        // +--------------+----+--------------+----+--------------+----+
        int mappedIndexInsideCache = indexStoreing[currentCache].fetch_add(1);
        if(mappedIndexInsideCache < cacheIndexCount)
        {
            int realCacheIndex =
                    mappedIndexInsideCache * (maxDataChunkSize_g + 4);
            int *chunkSizePointer =
                    (int*)(dataCache[currentCache] + realCacheIndex);
            *chunkSizePointer = chunkSize;

            // Write data after chunk size
            memcpy(&(dataCache[currentCache][realCacheIndex+4]), dataChunk,
                   chunkSize);

            if(indexStored[currentCache].fetch_add(1) == cacheIndexCount-1)
            {
                writeCache(currentCache, cacheIndexCount);
                indexStored[currentCache] = 0;
                indexStoreing[currentCache] = 0;
            }
            return;
        }
        else if(mappedIndexInsideCache == cacheIndexCount)
        {   // Index is now pointing out of the array
            int nextCacheNumber = currentCache+1;
            if(nextCacheNumber >= NUMBER_OF_CACHES)
                nextCacheNumber++;
            while(indexStoreing[nextCacheNumber].load() != 0)
                ; // Wait until data from next cache are written
            // Increment cache number
            cacheActualNumber.store(nextCacheNumber);
        }
    }
}
