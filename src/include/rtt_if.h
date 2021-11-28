#ifndef RTT_IF_H
#define RTT_IF_H
/* rtt i/o to terminal */

/* default buffer sizes, 8 bytes added to up buffer for alignment and padding */
/* override RTT_UP_BUF_SIZE and RTT_DOWN_BUF_SIZE in platform.h if needed */

#if !defined(RTT_UP_BUF_SIZE) || !defined(RTT_DOWN_BUF_SIZE)
#if (PC_HOSTED == 1)
#define RTT_UP_BUF_SIZE    (4096 + 8)
#define RTT_DOWN_BUF_SIZE  (512)
#elif defined(STM32F7)
#define RTT_UP_BUF_SIZE    (4096 + 8)
#define RTT_DOWN_BUF_SIZE  (2048)
#elif defined(STM32F4)
#define RTT_UP_BUF_SIZE    (2048 + 8)
#define RTT_DOWN_BUF_SIZE  (256)
#else /* stm32f103 */
#define RTT_UP_BUF_SIZE    (1024 + 8)
#define RTT_DOWN_BUF_SIZE  (256)
#endif
#endif

/* hosted initialisation */
extern int rtt_if_init(void);
/* hosted teardown */
extern int rtt_if_exit(void);

/* target to host: write len bytes from the buffer starting at buf. return number bytes written */
extern uint32_t rtt_write(const char *buf, uint32_t len);
/* host to target: read one character, non-blocking. return character, -1 if no character */
extern int32_t rtt_getchar();
/* host to target: true if no characters available for reading */
extern bool rtt_nodata();

#endif
