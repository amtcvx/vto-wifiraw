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

#define PORT_LOG  5000

// TIMER, PROTOCOL, VIDEO, TUNNEL, TELEMETRY
#define MAXDEV (1 + MAXRAWDEV) 

#endif // WFB_H
