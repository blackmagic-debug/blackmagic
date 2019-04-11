
/*==============================================================================
Copyright 2016 Microchip Technology Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "winc1500_api.h"
#include "wf_utils.h"

void DelayMs(uint16_t ms)
{
    uint32_t endTime = m2mStub_GetOneMsTimer() + ms + 1;
    
    while (m2mStub_GetOneMsTimer() <= endTime)
    {
        ;
    }
    
}


// convert ipV4 binary address to string (input IP address must be in big-endian format)
void inet_ntop4(uint32_t src, char *dest)
{
    uint8_t *p_byte = (uint8_t *)&src;
    sprintf(dest, "%d.%d.%d.%d", (int)p_byte[0], (int)p_byte[1], (int)p_byte[2], (int)p_byte[3]);
}


// converts IP string address to network byte order uint32_t binary address
#define SEP "."
int inet_pton4(const char *src, uint32_t *dst)
{
    char     *p_token;
    char     srcCopy[32];
    int      index = 0;
    int      ret = 1;  // presume success

    strcpy(srcCopy, src); // strtok won't work if inet_pton4 called with string directly (e.g. inet_pton4("192.168.1.1", &dst) )
                          // so need to copy to local array

    p_token = strtok(srcCopy, SEP);   // point to first token in string
    while (p_token != NULL)
    {
        if (index > 3)  // more than 4 tokens is an error
        {
            ret = 0;
            break;
        }

        ((uint8_t *)dst)[index] = atoi(p_token);    // convert token to integer and store in return value
        if (((uint8_t*)dst)[index] == 0)            // if atoi had an error
        {
            ret = 0;
            break;
        }
        ++index;
        p_token = strtok(NULL, SEP);
    }

    if ((ret == 1) && (index != 4))    // error if too few tokens
    {
        ret = 0;
    }

    return ret;
}


// determines the elapsed time, in ms, from the input start time and current time
#define MAX_TIMER_COUNT 0xffffffff
uint32_t m2m_get_elapsed_time(uint32_t startTime)
{
    uint32_t currentTime = m2mStub_GetOneMsTimer();
    
    if (currentTime >= startTime)
    {
        return (currentTime - startTime);
    }
    else
    {
        return (MAX_TIMER_COUNT - (startTime + 1) + currentTime);
    }
}



