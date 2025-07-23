#ifndef WFB_H
#define WFB_H

#if BOARD
#define DRONEID         1
//#define DRONEID       2
#else
#define NBDRONEID	1
//#define NBDRONEID	2
#endif // BOARD

#define TUNIP_BOARD	"10.0.1.2"
#define TUNIP_GROUND	"10.0.1.1"
#define IPBROAD		"255.255.255.0"

#define ADDR_LOCAL	"127.0.0.1" 

#define FEC_K		8
#define FEC_N		12

#define PAY_MTU		1400
#define PERIOD_DELAY_S	1


static int PORTS_VID[] = { 5600, 5700 };
static int PORTS_RAW[] = { 3000, 3500 };

static int INITCHAN[][2] = { { 12, 32 }, { 14 , 35 } };

#define WFBPORT  5000

#define TELPORTUP 4245
#define TELPORTDOWN 4244

#if BOARD
typedef enum { TIME_FD, RAW0_FD, RAW1_FD, 
	WFB_FD, TUN_FD, TEL_FD, VID_FD, 
	FD_NB } cannal_t;
#else 
typedef enum { TIME_FD, RAW0_FD, RAW1_FD, RAW2_FD, RAW3_FD,
	WFB1_FD, TUN1_FD, TEL1_FD, VID1_FD,
	WFB2_FD, TUN2_FD, TEL2_FD, VID2_FD,
	FD_NB } cannal_t;
#endif // BOARD

// cat /proc/sys/net/core/rmem_max rmem_default 212992 < 1400 x 154
#define STORE_SIZE	5

#endif // WFB_H
