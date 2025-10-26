/*
gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DCONFIG_LIBNL30 -I/usr/include/libnl3 -c sendnoizeraw.c -o sendnoizeraw.o

cc sendnoizeraw.o -g -lnl-route-3 -lnl-genl-3 -lnl-3 -o sendnoizeraw

sudo ./sendnoizeraw wlx3c7c3fa9bdc6 2427
*/
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

/************************************************************************************************/

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

/*****************************************************************************/
int main(int argc, char **argv) {

  uint8_t sockfd;
  ssize_t rawlen;
  uint16_t protocol = htons(ETH_P_ALL);
  if (-1 == (sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy( ifr.ifr_name, argv[1], sizeof( ifr.ifr_name ) - 1 );
  if (ioctl( sockfd, SIOCGIFINDEX, &ifr ) < 0 ) exit(-1);

  uint8_t sockid;
  struct nl_sock *socknl;
  if  (!(socknl = nl_socket_alloc())) exit(-1);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) exit(-1);
  if ((sockid = genl_ctrl_resolve(socknl, "nl80211")) < 0) exit(-1);
  struct nl_sock *sockrt;
  if (!(sockrt = nl_socket_alloc())) exit(-1);
  if (nl_connect(sockrt, NETLINK_ROUTE)) exit(-1);

  struct nl_msg *msg1 = nlmsg_alloc();
  genlmsg_put(msg1,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);
  nla_put_u32(msg1, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
  nla_put_u32(msg1, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
  if (nl_send_auto(socknl, msg1) < 0) exit(-1);
  nlmsg_free(msg1);

  struct nl_msg *msg2 = nlmsg_alloc();
  genlmsg_put(msg2,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,1);
  nla_put_u32(msg2, NL80211_ATTR_IFINDEX, ifr.ifr_ifindex);
  if (nl_send_auto(socknl, msg2) < 0) exit(-1);
  nlmsg_free(msg2);

  uint32_t freq = atoi(argv[2]);
  struct nl_msg *msg3=nlmsg_alloc();
  genlmsg_put(msg3,0,0,sockid,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(msg3,NL80211_ATTR_IFINDEX,ifr.ifr_ifindex);
  NLA_PUT_U32(msg3,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(socknl, msg3) < 0) exit(-1);
  nlmsg_free(msg3);


  uint8_t dumbuf[1400] = {-1};
  struct iovec iovdum = { .iov_base = dumbuf, .iov_len = sizeof(dumbuf) };
  struct iovec iovtab[4] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovdum };
  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 4 };

  rawlen = sendmsg(sockfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
  printf("(%ld)(%d)\n",rawlen,freq);

  nla_put_failure:
    exit(-1);
}
