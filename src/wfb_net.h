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
  uint8_t sockid;
  struct nl_sock *sockrt;
  uint8_t nbraws;
  wfb_net_device_t *rawdevs[MAXRAWDEV];
} wfb_net_init_t;


bool wfb_net_init(wfb_net_init_t *);
bool wfb_net_setfreq(uint8_t sockid, struct nl_sock *sockrt, int ifindex, uint32_t freq); 

/************************************************************************************************/
static uint8_t wfb_net_ieeehd_tx[] = {
  0x08, 0x01,                         // Frame Control : Data frame from STA to DS
  0x00, 0x00,                         // Duration
  0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
  0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
  0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
  0x10, 0x86                          // Sequence control
};

static uint8_t wfb_net_ieeehd_rx[24];

/************************************************************************************************/
#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x01
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x02
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04

#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20

#define IEEE80211_RADIOTAP_MCS_BW_20    0
#define IEEE80211_RADIOTAP_MCS_SGI      0x04

#define IEEE80211_RADIOTAP_MCS_STBC_1  1
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

#define MCS_KNOWN (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )

#define MCS_FLAGS  (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))

#define MCS_INDEX  2

static uint8_t wfb_net_radiotaphd_tx[] = {
    0x00, 0x00, // <-- radiotap version
    0x0d, 0x00, // <- radiotap header length
    0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
    0x08, 0x00,  // RADIOTAP_F_TX_NOACK
    MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};

static uint8_t wfb_net_radiotaphd_rx[35];


#endif // WFB_NET_H
