/*
 * CircularBuffer.h
 *
 *  Created on: 26.01.2018
 *      Author: Daniel
 */

#ifndef FIFO_H_
#define FIFO_H_

#include <stdint.h>
#include <stddef.h>

#define FIFO_STATIC(Name, Size)   \
    uint8_t Name##_Data[Size];     \
    Fifo_t Name = {               \
        .buffer = Name##_Data,       \
        .head = 0,                \
        .tail = 0,                \
        .bufflen = Size,         \
        .isfull = 0              \
    }

typedef struct {
    uint8_t *buffer;
    size_t head;
    size_t tail;
    const size_t bufflen;
    char isfull;
} Fifo_t;


int FifoPush(Fifo_t *c, const uint8_t byte);
uint8_t FifoPop(Fifo_t *c);
size_t FifoWrite(Fifo_t *c, const uint8_t *data, size_t size);
size_t FifoRead(Fifo_t *c, uint8_t *data, size_t size);
size_t FifoGetUsed(Fifo_t *c);
size_t FifoGetFree(Fifo_t *c);
uint8_t *FifoGetUnfragPointer(Fifo_t *c, size_t Size);
size_t FifoDel(Fifo_t *c, size_t del);
void FifoReset(Fifo_t *c);




#endif /* FIFO_H_ */
