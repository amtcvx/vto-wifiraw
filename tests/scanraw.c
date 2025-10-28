/*

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c scanraw.c -o scanraw.o

cc scanraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o scanraw

export DEVICE=wlx3c7c3fa9c1e4
sudo ./scanraw $DEVICE

export DEVICE=wlx3c7c3fa9c1e4
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set channel 3

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

/*****************************************************************************/
typedef struct {
  uint8_t cptfreqs;
  uint8_t nbfreqs;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
} rawdev_t;

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
int main(int argc, char **argv) {

  uint8_t fd[2];

  uint64_t exptime;
  if (-1 == (fd[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[0], 0, &period, NULL);

  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (fd[1] = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);

  struct nl_sock *socknl;
  uint8_t sockid;

  if  (!(socknl = nl_socket_alloc()))  exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);

  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy( ifr.ifr_name, argv[1], sizeof( ifr.ifr_name ) - 1 );
  if (ioctl( fd[1], SIOCGIFINDEX, &ifr ) < 0 ) exit(-1);

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

  static rawdev_t rawdev; memset(&rawdev, 0, sizeof(rawdev));
  bool msg_received = false;
  struct nl_cb *cb;
  if (!(cb = nl_cb_alloc(NL_CB_DEFAULT))) exit(-1);
  nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_callback, &msg_received);
  nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, getsinglewifi_callback, &rawdev);

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
  if (-1 == bind(fd[1], (struct sockaddr *)&sll, sizeof(sll))) exit(-1); // BIND must be AFTER wifi setting

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(fd[1], SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);

  uint8_t dumbuf[1500];
  struct iovec iov_dum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf)};
  struct iovec iovtab[1] = { iov_dum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 1 };

  ssize_t rawlen = 0, len = 0;
  uint8_t rawnb = 0;

  rawdev.cptfreqs = 0; 
  setfreq(sockid, socknl, ifr.ifr_ifindex, rawdev.freqs[rawdev.cptfreqs]);

  struct pollfd readsets[2] = { { .fd = fd[0], .events = POLLIN }, { .fd = fd[1], .events = POLLIN }};

  for(;;) {
    if (0 != poll(readsets, 2, -1)) {
      for (uint8_t cpt=0; cpt<2; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(fd[0], &exptime, sizeof(uint64_t));
	    printf("[%d] (%d)\n",rawdev.freqs[rawdev.cptfreqs],rawnb);
	    rawnb=0;
	    if (rawdev.cptfreqs < (rawdev.nbfreqs - 1)) rawdev.cptfreqs++; else rawdev.cptfreqs = 0;
	    setfreq(sockid, socknl, ifr.ifr_ifindex, rawdev.freqs[rawdev.cptfreqs]);
	  } else {
            rawlen = recvmsg(fd[1], &msg, MSG_DONTWAIT);
	    rawnb++;
	  }
	}
      }
    }
  }
}
