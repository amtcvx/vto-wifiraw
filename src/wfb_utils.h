#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <stdint.h>
#include <poll.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "wfb.h"

#define ONLINE_MTU PAY_MTU

#if RAW
#define wfb_utils_datapos 3
#else
#define wfb_utils_datapos 1
#endif

typedef struct {
  struct iovec head[wfb_utils_datapos + 1];
} wfb_utils_head_t;

typedef struct {
  uint8_t bufs[ ONLINE_MTU + 1];
  struct msghdr msg;
  struct iovec iovecs;
  wfb_utils_head_t headvecs;
} wfb_utils_rawmsg_t;

typedef struct {
  uint8_t readtabnb;
  uint8_t readtab[MAXDEV];
  struct pollfd readsets[MAXDEV];
  uint8_t nbdev;
  uint8_t rawlimit;
  uint8_t fd[MAXDEV];
  wfb_net_init_t *pnet;
} wfb_utils_init_t;

typedef struct {
  uint8_t droneid;
  uint8_t msgcpt;
  uint16_t msglen;
  uint8_t seq;
  uint8_t fec;
  uint8_t num;
} __attribute__((packed)) wfb_utils_pay_t;

void wfb_utils_periodic();
void wfb_utils_init(wfb_utils_init_t *putils);
void wfb_utils_presetrawmsg(wfb_utils_rawmsg_t *, bool);


#endif // WFB_UTILS_H
