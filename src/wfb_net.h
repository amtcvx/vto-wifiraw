#ifndef WFB_NET_H
#define WFB_NET_H

#include <stdint.h>
#include <stdbool.h>

#define MAXRAWDEV 20

#define NBFREQS 65
typedef struct {
  uint8_t sockfd;
  char drivername[30];
  char ifname[30];
  int ifindex;
  int iftype;
  uint8_t nbfreqs;
  uint8_t currchan;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
} wfb_net_device_t;

typedef struct {
  uint8_t *radiotaphd_tx;
  uint8_t radiotaphd_tx_size;
  uint8_t *ieeehd_tx;
  uint8_t ieeehd_tx_size;
} wfb_net_heads_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *sockrt;
  uint8_t nbraws;
  wfb_net_device_t *rawdevs[MAXRAWDEV];
  wfb_net_heads_t *heads;
} wfb_net_init_t;


bool wfb_net_init(wfb_net_init_t *);
bool wfb_net_setfreq(uint8_t sockid, struct nl_sock *sockrt, int ifindex, uint32_t freq); 

#endif // WFB_NET_H
