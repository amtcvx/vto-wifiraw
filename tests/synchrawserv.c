/*

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c synchrawserv.c -o synchrawserv.o

cc synchrawserv.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o synchrawserv

export DEVICE1=wlx3c7c3fa9c1e4
export DEVICE2=wlxfc349725a317

sudo ./synchrawserv $DEVICE1
sudo ./synchrawserv $DEVICE1 $DEVICE2
*/

#include<unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/uio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/filter.h>

#include <stdbool.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>

#include <netlink/route/link.h>
#include <net/if.h>

#include <sys/timerfd.h>

#include <errno.h>

/*****************************************************************************/
#define NBFREQS 65
#define PERIOD_DELAY_S  1
#define FREESECS  10
#define MAXRAWNB 4
#define MAXDEVNB 1 + MAXRAWNB

#define DRONEID_GRD 0
#define DRONEID_MIN 1
#define DRONEID_MAX 2

#define DRONEID 1

/*****************************************************************************/
#define NBFREQS 65
#define PAY_MTU 1400

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

/*****************************************************************************/
typedef enum { WFB_PRO, WFB_NB } type_d;

typedef struct {
  bool    freefreq;
  uint8_t syncelapse;
  uint8_t synccum;
  uint8_t ifindex;
  uint8_t fd;
  uint8_t cptfreqs;
  uint8_t nbfreqs;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
} rawdev_t;

/*****************************************************************************/
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
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fec_t;

typedef struct {
  int16_t chan;
} __attribute__((packed)) wfb_utils_pro_t;

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)

uint8_t radiotaphd_tx[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};
uint8_t ieeehd_tx[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
};
uint8_t llchd_tx[4] = {1,2,3,4};

struct iovec iov_radiotaphd_tx = { .iov_base = radiotaphd_tx, .iov_len = sizeof(radiotaphd_tx)};
struct iovec iov_ieeehd_tx =     { .iov_base = ieeehd_tx,     .iov_len = sizeof(ieeehd_tx)};
struct iovec iov_llchd_tx =      { .iov_base = llchd_tx,      .iov_len = sizeof(llchd_tx)};

/******************************************************************************/
int finish_callback(struct nl_msg *nlmsg, void *arg) {
  bool* finished = arg;
  *finished = true;
  return NL_SKIP;
}

/******************************************************************************/
int getsinglewifi_callback(struct nl_msg *nlmsg, void *arg) {

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(nlmsg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

  if (tb_msg[NL80211_ATTR_WIPHY_BANDS]) {
    struct nlattr *tb_band[NL80211_BAND_ATTR_MAX + 1];
    struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX + 1];
    struct nlattr *nl_band;
    struct nlattr *nl_freq;
    int rem_band, rem_freq;
    int last_band = -1;

    rawdev_t *ptr = ((rawdev_t *)arg);

    nla_for_each_nested(nl_band, tb_msg[NL80211_ATTR_WIPHY_BANDS], rem_band) {
      if (last_band != nl_band->nla_type) last_band = nl_band->nla_type;
      nla_parse(tb_band, NL80211_BAND_ATTR_MAX, nla_data(nl_band), nla_len(nl_band), NULL);
      if (tb_band[NL80211_BAND_ATTR_FREQS]) {
        nla_for_each_nested(nl_freq, tb_band[NL80211_BAND_ATTR_FREQS], rem_freq) {
          nla_parse(tb_freq, NL80211_FREQUENCY_ATTR_MAX, nla_data(nl_freq), nla_len(nl_freq), NULL);

          uint32_t freq = nla_get_u32(tb_freq[NL80211_FREQUENCY_ATTR_FREQ]);
          ptr->freqs[ptr->nbfreqs] = freq;

          if (freq == 2484) freq = 14;
          else if (freq < 2484) freq = (freq - 2407) / 5;
          else if (freq < 5000) freq = 15 + ((freq - 2512) / 20);
          else freq = ((freq - 5000) / 5);

          ptr->chans[ptr->nbfreqs] = freq;
          ptr->nbfreqs++;
        }
      }
    }
  }
  return NL_SKIP;
}

/*****************************************************************************/
bool setfreq(uint8_t sockid, struct nl_sock *socknl, int ifindex, uint32_t freq) {

  bool ret=true;
  struct nl_msg *nlmsg;
  if (!(nlmsg  = nlmsg_alloc())) exit(-1);;
  genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(nlmsg,NL80211_ATTR_IFINDEX,ifindex);
  NLA_PUT_U32(nlmsg,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(socknl, nlmsg) < 0) ret=false;
  nlmsg_free(nlmsg);
  return(ret);
  nla_put_failure:
    nlmsg_free(nlmsg);
    return(false);
}

/*****************************************************************************/
void setraw(uint8_t sockid, struct nl_sock *socknl, int argc, char **argv, rawdev_t rawdev[MAXRAWNB]) {

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  uint16_t protocol = htons(ETH_P_ALL);
  
  for (uint8_t cpt=0; cpt < (argc-1); cpt++) { 

    if (-1 == (rawdev[cpt].fd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy( ifr.ifr_name, argv[cpt+1], sizeof( ifr.ifr_name ) - 1 );
    if (ioctl( rawdev[cpt].fd, SIOCGIFINDEX, &ifr ) < 0 ) exit(-1);
    rawdev[cpt].ifindex = ifr.ifr_ifindex;
  
    struct nl_msg *nlmsg;
  
    if (!(nlmsg  = nlmsg_alloc())) exit(-1);;
    genlmsg_put(nlmsg,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
    nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
    nla_put_u32(nlmsg, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
    nl_send_auto(socknl, nlmsg);
    if (nl_send_auto(socknl, nlmsg) >= 0)  nl_recvmsgs_default(socknl);
    nlmsg_free(nlmsg);
  
    struct nl_cache *cache;
    struct rtnl_link *link, *change;
    if ((rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) < 0) exit(-1);
    if (!(link = rtnl_link_get(cache,ifr.ifr_ifindex))) exit(-1);
    if (!(rtnl_link_get_flags (link) & IFF_UP)) {
      change = rtnl_link_alloc ();
      rtnl_link_set_flags (change, IFF_UP);
      rtnl_link_change(sockrt, link, change, 0);
    }
  
    rawdev[cpt].nbfreqs = 0;
    bool msg_received = false;
    struct nl_cb *cb;
    if (!(cb = nl_cb_alloc(NL_CB_DEFAULT))) exit(-1);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_callback, &msg_received);
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, getsinglewifi_callback, &rawdev[cpt]);
  
    if (!(nlmsg  = nlmsg_alloc())) exit(-1);
    genlmsg_put(nlmsg, NL_AUTO_PORT, NL_AUTO_SEQ, sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
    nla_put_u32(nlmsg, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
    nl_send_auto(socknl, nlmsg);
    msg_received = false;
    while (!msg_received) nl_recvmsgs(socknl, cb);
    nlmsg_free(nlmsg);
  
    struct sockaddr_ll sll;
    memset( &sll, 0, sizeof( sll ) );
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_protocol = protocol;
    if (-1 == bind(rawdev[cpt].fd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // BIND must be AFTER wifi setting
  
    const int32_t sock_qdisc_bypass = 1;
    if (-1 == setsockopt(rawdev[cpt].fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);
  }

}

/*****************************************************************************/
int main(int argc, char **argv) {

  if (!((argc >= 1) && (argc <= 3))) exit(-1);
  printf("START [%d]\n",argc);

  uint8_t probuf[MAXRAWNB][sizeof(wfb_utils_pro_t)];
  ssize_t lentab[WFB_NB][MAXRAWNB];
  memset(lentab, 0, sizeof(lentab));

  struct pollfd readsets[MAXDEVNB];
  uint8_t nbfd = 0;

  uint64_t exptime;
  if (-1 == (readsets[nbfd].fd = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(readsets[nbfd].fd, 0, &period, NULL);
  nbfd++;

  uint8_t sockid; struct nl_sock *socknl;
  if  (!(socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  rawdev_t rawdev[MAXRAWNB]; memset(&rawdev,0,sizeof(rawdev));
  setraw(sockid, socknl, argc, argv, rawdev);
  uint8_t minraw = 1, maxraw = 3;

  if (rawdev[1].freefreq) printf("OK\n");
  else printf("KO\n");

  rawdev[0].cptfreqs = 0; 
  setfreq(sockid, socknl, rawdev[0].ifindex, rawdev[0].freqs[rawdev[0].cptfreqs]);
  readsets[nbfd].fd = rawdev[0].fd;
  uint8_t rawnb = 1;
  nbfd++;

  if (argc == 3) { 
    rawdev[1].cptfreqs =  rawdev[1].nbfreqs / 2;  
    setfreq(sockid, socknl, rawdev[1].ifindex, rawdev[1].freqs[rawdev[1].cptfreqs]);
    readsets[nbfd].fd = rawdev[1].fd;
    rawnb++;
    nbfd++;
  }
  for (uint8_t cpt=0;cpt < nbfd; cpt++) readsets[cpt].events = POLLIN;

  uint8_t sequence = 0, num = 0;
  uint8_t dumbuf[1500];
  struct iovec iov_dum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf)};
  struct iovec iovtab[1] = { iov_dum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 1 };

  ssize_t rawlen = 0, len = 0;

  int8_t mainraw = -1, backraw = -1, tmpraw = -1;
  for(;;) {
    if (0 != poll(readsets, nbfd, -1)) {
      for (uint8_t cpt=0; cpt<nbfd; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(readsets[cpt].fd, &exptime, sizeof(uint64_t));


	    printf("\n(%d)(%d)  (%d)(%d)\n",rawdev[0].freqs[rawdev[0].cptfreqs], rawdev[0].synccum, rawdev[1].freqs[rawdev[1].cptfreqs], rawdev[1].synccum);


	    for (uint8_t rawcpt = 0; rawcpt < rawnb; rawcpt++) {
	      if (rawdev[rawcpt].synccum != 0) { rawdev[rawcpt].freefreq = false; rawdev[rawcpt].syncelapse = 0; }
	      else if (rawdev[rawcpt].syncelapse < FREESECS) rawdev[rawcpt].syncelapse++; else { rawdev[rawcpt].freefreq = true; rawdev[rawcpt].syncelapse = 0; }
	      rawdev[rawcpt].synccum = 0;
	    }

	    if (mainraw < 0) {
	      for (uint8_t rawcpt = 0; rawcpt < rawnb; rawcpt++) if (rawdev[rawcpt].freefreq) mainraw = rawcpt;
	    } else {
	      if (!(rawdev[mainraw].freefreq)) {
                if (backraw < 0) { for (uint8_t rawcpt = 0; rawcpt < rawnb; rawcpt++) if (rawdev[rawcpt].freefreq) mainraw = rawcpt; }
	        else if (rawdev[backraw].freefreq) { mainraw = backraw; backraw = -1; }
	      }
	    }

	    if ((mainraw >=0) && (backraw < 0)) {
	      for (uint8_t rawcpt = 0; rawcpt < rawnb; rawcpt++) if ((rawdev[rawcpt].freefreq) && (rawcpt != mainraw)) backraw = rawcpt;
	    }

	    for (uint8_t rawcpt = 0; rawcpt < rawnb; rawcpt++) {
              if (((rawcpt != mainraw) && (rawcpt != backraw)) &&
                  ((!(rawdev[rawcpt].freefreq) && (rawdev[rawcpt].syncelapse == 0)))) {

	        if (rawdev[rawcpt].cptfreqs < (rawdev[rawcpt].nbfreqs - 1)) rawdev[rawcpt].cptfreqs++; else rawdev[rawcpt].cptfreqs = 0;
                setfreq(sockid, socknl, rawdev[rawcpt].ifindex, rawdev[rawcpt].freqs[rawdev[rawcpt].cptfreqs]);
	      }
	    }

	    if (mainraw >= 0) {
              lentab[WFB_PRO][mainraw] = sizeof(wfb_utils_pro_t);
              ((wfb_utils_pro_t *)&probuf[mainraw])->chan = -1;
	      if (backraw >= 0) {
                ((wfb_utils_pro_t *)&probuf[mainraw])->chan = rawdev[backraw].freqs[rawdev[backraw].cptfreqs];
                ((wfb_utils_pro_t *)&probuf[backraw])->chan = -rawdev[mainraw].freqs[rawdev[mainraw].cptfreqs]; 
		lentab[WFB_PRO][backraw] = sizeof(wfb_utils_pro_t);
	      }
	    }

	    printf("(%d)(%d)\n",mainraw,backraw);

	  } else {
            rawlen = recvmsg(readsets[cpt].fd, &msg, MSG_DONTWAIT);
	    rawdev[cpt-1].synccum++;
	  }
	}
      }

      uint8_t kmin=0, kmax=1;

      for (uint8_t k=kmin;k<kmax;k++) {
        for (uint8_t d=0; d < WFB_NB; d++) {
          for (uint8_t c = 0; c < (maxraw - minraw); c++) {

            if (lentab[d][c] > 0) {

              struct iovec iovpay;

              if (d == WFB_PRO) { iovpay.iov_base = &probuf[c]; iovpay.iov_len = lentab[WFB_PRO][c]; };

              wfb_utils_heads_pay_t headspay =
                { .droneid = DRONEID, .msgcpt = d, .msglen = lentab[d][c], .seq = sequence, .fec = k, .num = num++ };
              struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
              struct iovec iovtab[5] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovheadpay, iovpay }; uint8_t msglen = 5;
              struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = msglen };
              len = sendmsg(rawdev[ c ].fd, (const struct msghdr *)&msg, MSG_DONTWAIT);

              printf("(%d) sendmsg (%ld)(%d)\n", c, len, ((wfb_utils_pro_t *)&probuf[c])->chan );

              lentab[d][c] = 0;
            }
          }
        }
      }
    }
  }
}
