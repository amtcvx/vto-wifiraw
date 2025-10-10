#ifndef WFB_UTILS_H
#define WFB_UTILS_H

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


#include "zfex.h"

#if TELEM
typedef enum { WFB_TIM, WFB_RAW, WFB_TUN, WFB_VID, WFB_TEL, WFB_NB } type_d;
#else
typedef enum { WFB_TIM, WFB_RAW, WFB_TUN, WFB_VID, WFB_NB } type_d;
#endif // TELEM
     
#define PAY_MTU 1400

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)

#define MAXNBRAWBUF 2*FEC_N

#define MAXDEV 25 

#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

#define PORT_NORAW   3000
#define PORT_VID     5600
#define PORT_TELUP   4245
#define PORT_TELDOWN 4244
#define PORT_LOG     5000

#define PAY_MTU 1400

#define TUN_MTU      1400
#define TUNIP_BOARD  "10.0.1.2"
#define TUNIP_GROUND "10.0.1.1"
#define IPBROAD      "255.255.255.0"

#define DRONEID_GRD 0
#define DRONEID_MIN 1
#define DRONEID_MAX 2

#if BOARD
#define DRONEID 1
#else
#define DRONEID DRONEID_GRD
#endif // BOARD

#define FEC_K   8
#define FEC_N   12

typedef struct {
  uint8_t fd;
  struct sockaddr_in addr;
  uint8_t txt[1000];
  uint16_t len;
} wfb_utils_log_t;

typedef struct {
  struct pollfd readsets[MAXDEV];
  uint8_t fd[MAXDEV];
  uint8_t readtab[WFB_NB];
  uint8_t socktab[WFB_NB];
  uint8_t readnb;
  struct sockaddr_in norawoutaddr;
  struct sockaddr_in vidoutaddr;
  struct sockaddr_in teloutaddr;
  fec_t *fec_p;
  wfb_utils_log_t log;
} wfb_utils_init_t;


void wfb_utils_init(wfb_utils_init_t *putils);


#endif // WFB_UTILS_H
