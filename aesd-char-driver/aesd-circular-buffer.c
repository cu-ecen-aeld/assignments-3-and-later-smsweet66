/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#include "aesd-circular-buffer.h"

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#define FREE(pointer) kfree(pointer)
#else
#include <string.h>
#define FREE(pointer)
#endif

void clear_buffer(AesdCircularBuffer *buffer)
{
    for (size_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i)
    {
        FREE(buffer->entry[i].buffptr);
        buffer->entry[i].buffptr = NULL;
        buffer->entry[i].size = 0;
    }
}

AesdBufferEntry *next_entry(AesdCircularBuffer *buffer, AesdBufferEntry *entry)
{
    size_t entry_index = index_of(buffer, entry);
    if (entry_index == -1)
    {
        return NULL;
    }

    entry_index = (entry_index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (entry_index == buffer->in_offs)
    {
        return NULL;
    }

    return &buffer->entry[entry_index];
}

AesdBufferEntry *previous_entry(AesdCircularBuffer *buffer, AesdBufferEntry *entry)
{
    size_t entry_index = index_of(buffer, entry);
    if (entry_index == -1 || entry_index == buffer->out_offs)
    {
        return NULL;
    }

    entry_index = (entry_index + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    return &buffer->entry[entry_index];
}

size_t index_of(AesdCircularBuffer *buffer, AesdBufferEntry *entry)
{
    size_t entry_index;
    if (entry < (AesdBufferEntry *)buffer->entry)
    {
        return -1;
    }

    entry_index = (entry - (AesdBufferEntry *)buffer->entry) / sizeof(AesdBufferEntry);
    if (entry_index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        return -1;
    }

    return entry_index;
}

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
AesdBufferEntry *aesd_circular_buffer_find_entry_offset_for_fpos(AesdCircularBuffer *buffer,
                                                                 size_t char_offset, size_t *entry_offset_byte_rtn)
{
    AesdBufferEntry *entry;
    if (buffer->out_offs == buffer->in_offs && !buffer->full)
    {
        *entry_offset_byte_rtn = char_offset;
        return NULL;
    }

    entry = &buffer->entry[buffer->out_offs];

    if (char_offset < entry->size)
    {
        *entry_offset_byte_rtn = char_offset;
        return entry;
    }

    char_offset -= entry->size;
    *entry_offset_byte_rtn = char_offset;
    for (uint8_t i = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i != buffer->in_offs; i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        entry = &buffer->entry[i];
        if (char_offset < entry->size)
        {
            return entry;
        }

        char_offset -= entry->size;
        *entry_offset_byte_rtn = char_offset;
    }

    return NULL;
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location.
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by the caller.
 * After the allocation, the circular buffer will free the allocated memory when necessary.
 */
AesdBufferEntry *aesd_circular_buffer_add_entry(AesdCircularBuffer *buffer, AesdBufferEntry *entry)
{
    AesdBufferEntry *buffptr = &buffer->entry[buffer->in_offs];
    buffer->entry[buffer->in_offs].buffptr = entry->buffptr;
    buffer->entry[buffer->in_offs].size = entry->size;
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (buffer->full)
    {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        return buffptr;
    }
    else if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }

    return NULL;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(AesdCircularBuffer *buffer)
{
    memset(buffer, 0, sizeof(AesdCircularBuffer));
}
