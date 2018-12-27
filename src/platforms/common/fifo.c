/*
 * CircularBuffer.c
 *
 *  Created on: 26.01.2018
 *      Author: Daniel
 */

#include <stdlib.h>
#include <string.h>
#include "fifo.h"


size_t FifoGetUsed(Fifo_t *c)
{
  if (c->isfull) return c->bufflen;
  if (c->tail > c->head) return c->bufflen + c->head - c->tail;
  else return c->head - c->tail;
}

size_t FifoGetFree(Fifo_t *c)
{
  return c->bufflen - FifoGetUsed(c);
}

int FifoPush(Fifo_t *c, const uint8_t byte)
{
  if (c->isfull) return 1;

  c->buffer[c->head] = byte;
  (c->head)++;
  c->head = c->head % c->bufflen;

  if (c->head == c->tail) c->isfull = 1;

  return 0;
}

uint8_t FifoPop(Fifo_t *c)
{
  if (c->head == c->tail && !c->isfull) return 0;

  uint8_t Res = c->buffer[c->tail];
  (c->tail)++;
  c->tail = c->tail % c->bufflen;

  if (c->isfull) c->isfull = 0;

  return Res;
}

size_t FifoWrite(Fifo_t *c, const uint8_t *data, size_t size)
{
  if (size > FifoGetFree(c)) size = FifoGetFree(c);

  size_t written;
  for (written = 0; written < size; written++)
  {
    FifoPush(c, data[written]);
  }

  return written;
}

size_t FifoRead(Fifo_t *c, uint8_t *data, size_t size)
{
  if (size > FifoGetUsed(c)) size = FifoGetUsed(c);

  size_t read;
  for (read = 0; read < size; read++)
  {
    data[read] = FifoPop(c);
  }

  return read;
}

uint8_t *FifoGetUnfragPointer(Fifo_t *c, size_t Size)
{
  size_t tailsize = c->bufflen - c->tail;

  if ((Size <= tailsize) || (c->tail <= c->head && !c->isfull))
  {
    return &c->buffer[c->tail];
  }

  uint8_t *tailstart = &c->buffer[c->tail];


  uint8_t *overlap = NULL;
  size_t overlapsize = 0;

  if ((tailsize + c->head) > c->tail) //used size overlapping tail? save it!
  {
    overlapsize = tailsize + c->head - c->tail;
    overlap = malloc(overlapsize);
    memcpy(overlap, &c->buffer[c->tail], overlapsize);

    tailstart = &c->buffer[(c->tail + overlapsize) % c->bufflen]; //% prevents tailstart pointing outside array
    tailsize -= overlapsize;
  }

  memmove(&c->buffer[overlapsize + tailsize], c->buffer, c->head);


  memcpy(&c->buffer[overlapsize], tailstart, tailsize);

  if (overlap)
  {
    memcpy(c->buffer, overlap, overlapsize);
    free(overlap);
  }

  c->tail = 0;
  c->head = (overlapsize + tailsize + c->head) % c->bufflen;

  return c->buffer;
}

size_t FifoDel(Fifo_t *c, size_t del)
{
  if (del > FifoGetUsed(c)) del = FifoGetUsed(c);

  if (del && c->isfull) c->isfull = 0;
  c->tail += del % c->bufflen;

  return del;
}

void FifoReset(Fifo_t *c)
{
  c->head = c->tail = 0;
  c->isfull = 0;
}
