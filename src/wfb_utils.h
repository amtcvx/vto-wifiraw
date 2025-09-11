#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <stdint.h>
#include <poll.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "zfex.h"
#include "wfb.h"

#define ONLINE_MTU PAY_MTU

#define MAXNBMTUIN FEC_N + WFB_NB

#if RAW
#define wfb_utils_datapos 3
#else
#define wfb_utils_datapos 1
#endif


typedef struct {
  int16_t chan;
} __attribute__((packed)) wfb_utils_pro_t;

typedef struct {
  uint8_t droneid;
  uint8_t msgcpt;
  uint16_t msglen;
  uint8_t seq;
  uint8_t fec;
  uint8_t num;
  uint8_t dum;
} __attribute__((packed)) wfb_utils_heads_pay_t;

typedef struct {
  uint8_t seq;
  uint8_t num;
} msg_eltout_t; 

typedef struct {
  uint8_t buf_pro[MAXRAWDEV][sizeof(wfb_utils_pro_t)];
  uint8_t buf_tun[ONLINE_MTU];
  uint8_t buf_tel[ONLINE_MTU];
  uint8_t buf_vid[FEC_N][ONLINE_MTU];
  struct iovec iov[WFB_NB][MAXRAWDEV][FEC_N];
  uint8_t currvid;
  msg_eltout_t eltout[MAXRAWDEV];
} wfb_utils_msgout_t;

typedef struct {
  uint8_t buf_raw[MAXNBMTUIN][ONLINE_MTU];
  struct iovec iovraw[MAXNBMTUIN];
  struct iovec iovfec[FEC_N];
  uint8_t curr;
  uint8_t curseq;
  uint8_t nxtseq;
  uint8_t nxtfec;
  bool fails;
} msg_eltin_t; 

typedef struct {
  msg_eltin_t eltin[MAXRAWDEV];
} wfb_utils_msgin_t;

typedef struct {
  uint8_t radiotaphd_rx[35];
  uint8_t ieeehd_rx[24];
  uint8_t llchd_rx[4];
} wfb_utils_heads_rx_t;

typedef struct {
  wfb_utils_heads_pay_t headspay;
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
  int8_t mainraw;
  int8_t backraw;
} wfb_utils_rawchan_t;


typedef struct {
  uint8_t readtabnb;
  struct pollfd readsets[MAXDEV];
  uint8_t nbdev;
  uint8_t nbraws;
  uint8_t fd[MAXDEV];
  wfb_utils_log_t log;
  wfb_utils_rawchan_t rawchan;
  wfb_utils_raw_t raws;
  wfb_net_socktidnl_t *sockidnl;
  wfb_net_device_t *rawdevs[MAXRAWDEV];
  wfb_utils_msgin_t msgin;
  wfb_utils_msgout_t msgout;
  struct sockaddr_in vidout;
  struct sockaddr_in norawout;
  fec_t *fec_p;
} wfb_utils_init_t;


void wfb_utils_periodic(wfb_utils_init_t *putils);
void wfb_utils_init(wfb_utils_init_t *putils);


#endif // WFB_UTILS_H
