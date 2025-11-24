#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <stdio.h>
#include <stdbool.h>
#include <poll.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <linux/if_tun.h>

#include <unistd.h>

#include "zfex.h"

#if TELEM
typedef enum { WFB_PRO, WFB_VID, WFB_TUN, WFB_TEL, WFB_NB } type_d;
#else
typedef enum { WFB_PRO, WFB_VID, WFB_TUN, WFB_NB } type_d;
#endif // TELEM
     
#define PAY_MTU 1400

#define MAXNBRAWBUF 2*FEC_N

#if RAW
#include "wfb_net.h"
#define MAXDEV WFB_NB + MAXRAWDEV
#else
#define MAXDEV WFB_NB + 1
#endif // RAW


#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

#define PORT_NORAW   3000
#define PORT_VID     5600
#define PORT_LOG     5000

#if TELEM
#define PORT_TEL_UP   4245
#define PORT_TEL_DOWN 4244
#endif // TELEM

#define PAY_MTU 1400

#define DRONEID_GRD 0
#define DRONEID_MIN 1
#define DRONEID_MAX 2

#if BOARD
#define DRONEID 1
#else
#define DRONEID DRONEID_GRD
#endif // BOARD


#define TUN_IP_BOARD	"10.0.1.2"
#define TUN_IP_GROUND	"10.0.1.1"
#define TUN_MTU		1400
#define IPBROAD		"255.255.255.0"

#if BOARD
#define TUN_NAME	"artun"
#define TUN_IP_SRC	TUN_IP_BOARD
#define TUN_IP_DST	TUN_IP_GROUND
#else
#define TUN_NAME	"grdtun"
#define TUN_IP_SRC	TUN_IP_GROUND
#define TUN_IP_DST	TUN_IP_BOARD
#endif // BOARD


#define FEC_K   8
#define FEC_N   12

typedef struct {
  int16_t chan;
  uint8_t droneid;
  uint8_t msgcpt;
  uint16_t msglen;
  uint8_t seq;
  uint8_t fec;
  uint8_t num;
  uint8_t dum;
} __attribute__((packed)) wfb_utils_heads_pay_t;

typedef struct {
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fechd_t;

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fechd_t)

#if BOARD
#else
typedef struct {
  uint8_t outblockrecov[FEC_N-FEC_K];
  uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];
  uint8_t *outblocks[FEC_N-FEC_K];
  unsigned index[FEC_K];
  uint8_t *inblocks[FEC_K+1];
  uint8_t alldata;
  uint8_t inblocksnb;
  uint8_t recovcpt;
  int8_t inblockstofec;
  int8_t failfec;
  uint8_t msginnxtfec;
  int16_t msginnxtseq;
  int16_t msgincurseq;
  bool bypassflag;
  struct sockaddr_in vidoutaddr;
  uint8_t fdvid;
} wfb_utils_fec_t;
#endif // BOARD

typedef struct {
  uint8_t fd;
  struct sockaddr_in addr;
  uint8_t txt[1000];
  uint16_t len;
} wfb_utils_log_t;

typedef struct {
  struct pollfd readsets[MAXDEV];
  uint8_t fd[MAXDEV];
  uint8_t readtab[MAXDEV];
  uint8_t socktab[MAXDEV];
  uint8_t readnb;
  struct sockaddr_in norawoutaddr;
  wfb_utils_log_t log;
  fec_t *fec_p;
#if BOARD
#else
  wfb_utils_fec_t fec;
#if TELEM
  struct sockaddr_in teloutaddr;
#endif // TELEM
#endif // BOARD
} wfb_utils_init_t;


void wfb_utils_init(wfb_utils_init_t *u);

#if RAW
void wfb_utils_periodic(wfb_utils_init_t *u, wfb_net_init_t *n,ssize_t lentab[WFB_NB][MAXRAWDEV] ,int16_t probuf[MAXRAWDEV]); 
void wfb_utils_addraw(wfb_utils_init_t *u, wfb_net_init_t *n);
#if BOARD
#else // BOARD
void wfb_utils_syncground(wfb_utils_init_t *u, wfb_net_init_t *n, uint8_t rawcpt);
#endif // BOARD
#endif // RAW

#if BOARD
#else
void wfb_utils_sendfec(wfb_utils_fec_t *uf, fec_t *fec_p, uint8_t hdseq,  uint8_t hdfec, void *base);
#endif // BOARD


#endif // WFB_UTILS_H
