#ifndef WFB_NET_H
#define WFB_NET_H

#include <stdint.h>
#include <stdbool.h>

#define MAXRAWDEV 4

#define NBFREQS 65

extern struct iovec iov_radiotaphd_tx;
extern struct iovec iov_ieeehd_tx;
extern struct iovec iov_llchd_tx;
extern struct iovec iov_radiotaphd_rx;
extern struct iovec iov_ieeehd_rx;
extern struct iovec iov_llchd_rx;

typedef struct {
  int8_t mainraw;
  int8_t backraw;
} wfb_net_rawchan_t;

typedef struct {
  bool    freqfree;
  bool    syncelapse; 
  int16_t syncchan;
  uint8_t timecpt;
  uint8_t freqnb;
  uint32_t fails;
  uint32_t sent;
} wfb_net_status_t;

typedef struct {
  uint8_t sockfd;
  char drivername[30];
  char ifname[30];
  int ifindex;
  int iftype;
  uint8_t nbfreqs;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
  wfb_net_status_t stat;
} wfb_net_device_t;

typedef struct {
  uint8_t sockid;
  struct nl_sock *socknl;
} wfb_net_sockidnl_t;

typedef struct {
  uint8_t nbraws;
  wfb_net_rawchan_t rawchan;
  wfb_net_sockidnl_t sockidnl;
  wfb_net_device_t *rawdevs[MAXRAWDEV];
} wfb_net_init_t;


bool wfb_net_init(wfb_net_init_t *);
void wfb_net_drain(uint8_t fd);
bool wfb_net_setfreq(wfb_net_sockidnl_t *psock, int ifindex, uint32_t freq); 

#endif // WFB_NET_H
