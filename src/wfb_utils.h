#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <stdint.h>
#include <poll.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "wfb.h"

#define MAXRAWMSG 20

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
  uint8_t droneid;
  uint8_t msgcpt;
  uint16_t msglen;
  uint8_t seq;
  uint8_t fec;
  uint8_t num;
} __attribute__((packed)) wfb_utils_pay_t;

typedef struct {
  uint8_t ieeehd_rx[24];
  uint8_t radiotaphd_rx[35];
} wfb_utils_heads_rx_t;

typedef struct {
  uint8_t rawmsgcurr;
  wfb_utils_rawmsg_t rawmsg[MAXRAWMSG];
  wfb_utils_pay_t pay;
  wfb_net_heads_tx_t *headstx;
  wfb_utils_heads_rx_t *headsrx;
} wfb_utils_raw_t;

typedef struct {
  uint8_t fd;
  struct sockaddr_in addrout;
  uint8_t txt[1000];
  uint16_t len;
} wfb_utils_log_t;

typedef struct {
  uint8_t readtabnb;
  uint8_t readtab[MAXDEV];
  struct pollfd readsets[MAXDEV];
  uint8_t nbdev;
  uint8_t rawlimit;
  uint8_t nbraws;
  uint8_t fd[MAXDEV];
  wfb_utils_log_t stat;
  wfb_utils_raw_t raws;
  wfb_net_socktidnl_t *sockidnl;
  wfb_net_device_t *rawdevs;
} wfb_utils_init_t;

void wfb_utils_periodic(wfb_utils_init_t *putils);
void wfb_utils_init(wfb_utils_init_t *putils);
void wfb_utils_presetrawmsg(wfb_utils_raw_t *raw, bool rxflag);


#endif // WFB_UTILS_H
