#ifndef WFB_H
#define WFB_H

#include "wfb_net.h"

#if BOARD
#define DRONEID 1
#else
#define DRONEID 0
#endif // BOARD

#define DRONEIDMAX 2
#define DRONEIDMIN 1

#define PERIOD_DELAY_S	1

#define PAY_MTU	1400

#define IP_LOCAL "127.0.0.1"

#define TUNIP_BOARD	"10.0.1.2"
#define TUNIP_GROUND	"10.0.1.1"
#define IPBROAD		"255.255.255.0"

#define PORT_LOG  5000

typedef enum { WFB_PRO, WFB_TUN, WFB_TEL, WFB_VID, WFB_NB } type_d;

#define MAXDEV (1 + MAXRAWDEV) 

#define FEC_K	8
#define FEC_N	12

#endif // WFB_H
