#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <netlink/netlink.h> 
#include <netlink/genl/genl.h> 
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h> 
#include <linux/nl80211.h> 

#include <netlink/route/link.h>
#include <net/if.h>

#include <dirent.h>

#include <net/ethernet.h>
#include <linux/filter.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>

#include "wfb_net.h"

const char * drivername_arr[] =  { "rt2800usb", "rtl88XXau", "rtw_8822bu" };

struct netlink_t {
  int    id;
  struct nl_sock *socket;
} g_netlink;

#define NBFREQS 65
typedef struct {
  char drivername[30];
  char ifname[30];
  int ifindex;
  int iftype;
  uint8_t nbfreqs;
  uint8_t currchan;
  uint32_t freqs[NBFREQS];
  uint32_t chans[NBFREQS];
} device_t;

typedef struct {
  uint8_t current;
  uint8_t nb;
  device_t devs[MAXRAWDEV];
} elt_t;

/******************************************************************************/
struct iovec wfb_net_ieeehd_tx_vec = { .iov_base = &wfb_net_ieeehd_tx, .iov_len = sizeof(wfb_net_ieeehd_tx)};
struct iovec wfb_net_ieeehd_rx_vec = { .iov_base = &wfb_net_ieeehd_rx, .iov_len = sizeof(wfb_net_ieeehd_rx)};

struct iovec wfb_net_radiotaphd_tx_vec = { .iov_base = &wfb_net_radiotaphd_tx, .iov_len = sizeof(wfb_net_radiotaphd_tx)};
struct iovec wfb_net_radiotaphd_rx_vec = { .iov_base = &wfb_net_radiotaphd_rx, .iov_len = sizeof(wfb_net_radiotaphd_rx)};

/******************************************************************************/
static int finish_callback(struct nl_msg *msg, void *arg) {
  bool* finished = arg;
  *finished = true;
  return NL_SKIP;
}

/******************************************************************************/
static int getallinterfaces_callback(struct nl_msg *msg, void *arg) {
  device_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->nb]);
  ((((elt_t *)arg)->nb)++);

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);
  if (tb_msg[NL80211_ATTR_IFNAME]) strcpy(ptr->ifname, nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
  if (tb_msg[NL80211_ATTR_IFINDEX]) ptr->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
  if (tb_msg[NL80211_ATTR_IFTYPE]) ptr->iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);

  return NL_SKIP;
}

/******************************************************************************/
static int getsinglewifi_callback(struct nl_msg *msg, void *arg) {
  device_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->current]);

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);
  if (tb_msg[NL80211_ATTR_WIPHY_BANDS]) {
    struct nlattr *tb_band[NL80211_BAND_ATTR_MAX + 1];
    struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX + 1];
    struct nlattr *nl_band;
    struct nlattr *nl_freq;
    int rem_band, rem_freq;
    int last_band = -1;
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

/******************************************************************************/
static void unblock_rfkill(elt_t *elt) {
  char *ptr,*netpath = "/sys/class/net";
  char path[1024],buf[1024];
  ssize_t lenlink;
  struct dirent *dir1;
  DIR *d1;
  FILE *fd;

  for(uint8_t i=0;i<elt->nb;i++) {
    sprintf(path,"%s/%s/device/driver",netpath,elt->devs[i].ifname);
    if ((lenlink = readlink(path, buf, sizeof(buf)-1)) != -1) {
      buf[lenlink] = '\0';
      ptr = strrchr( buf, '/' );
      strcpy(elt->devs[i].drivername, ++ptr);
    }

    sprintf(path,"%s/%s/phy80211",netpath,elt->devs[i].ifname);
    d1 = opendir(path);
    while ((dir1 = readdir(d1)) != NULL)
      if ((strncmp("rfkill",dir1->d_name,5)) == 0) break;
    if ((strncmp("rfkill",dir1->d_name,6)) == 0) {
      sprintf(path,"%s/%s/phy80211/%s/soft",netpath,elt->devs[i].ifname,dir1->d_name);
      fd = fopen(path,"r+");
      if (fgetc(fd)==49) {
        fseek(fd, -1, SEEK_CUR);
        fputc(48, fd);
      };
      fclose(fd);
    }
  }
}

/******************************************************************************/
static int setwifi(elt_t *elt) {

  bool msg_received = false;

  struct nl_cb *cb1 = nl_cb_alloc(NL_CB_DEFAULT);
  if (!cb1) return ENOMEM;
  nl_cb_set(cb1, NL_CB_VALID, NL_CB_CUSTOM, getallinterfaces_callback, elt);
  nl_cb_set(cb1, NL_CB_FINISH, NL_CB_CUSTOM, finish_callback, &msg_received);

  struct nl_msg *msg1 = nlmsg_alloc();
  if (!msg1) return -2;
  genlmsg_put(msg1, NL_AUTO_PORT, NL_AUTO_SEQ, g_netlink.id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
  nl_send_auto(g_netlink.socket, msg1);
  msg_received = false;
  while (!msg_received) nl_recvmsgs(g_netlink.socket, cb1);
  nlmsg_free(msg1);

  if (elt->nb == 0) return(0);

  struct nl_sock *sockrt = nl_socket_alloc();
  if (!sockrt) return -ENOMEM;
  if (nl_connect(sockrt, NETLINK_ROUTE)) {
    nl_close(sockrt);
    nl_socket_free(sockrt);
    return -ENOLINK;
  }

  for(uint8_t i=0;i<elt->nb;i++) {
    struct nl_msg *msg3 = nlmsg_alloc();
    if (!msg3) return -2;
    genlmsg_put(msg3,0,0,g_netlink.id,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
    nla_put_u32(msg3, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nla_put_u32(msg3, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
    nl_send_auto(g_netlink.socket, msg3);
    if (nl_send_auto(g_netlink.socket, msg3) >= 0)  nl_recvmsgs_default(g_netlink.socket);
    nlmsg_free(msg3);
  }

  elt->nb = 0;
  struct nl_msg *msg4 = nlmsg_alloc();
  if (!msg4) return -2;
  genlmsg_put(msg4, NL_AUTO_PORT, NL_AUTO_SEQ, g_netlink.id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
  nl_send_auto(g_netlink.socket, msg4);
  msg_received = false;
  while (!msg_received) nl_recvmsgs(g_netlink.socket, cb1);
  nlmsg_free(msg4);

  unblock_rfkill(elt);

  int8_t err = 0;
  struct nl_cache *cache;
  struct rtnl_link *link, *change;
  if ((err = rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) >= 0) {
    for(uint8_t i=0;i<elt->nb;i++) {
      if ((link = rtnl_link_get(cache, elt->devs[i].ifindex))) {
        if (!(rtnl_link_get_flags (link) & IFF_UP)) {
          change = rtnl_link_alloc ();
          rtnl_link_set_flags (change, IFF_UP);
          rtnl_link_change(sockrt, link, change, 0);
	}
      }
    }
  }

  nl_cb_set(cb1, NL_CB_VALID, NL_CB_CUSTOM, getsinglewifi_callback, elt);
  for(uint8_t i=0;i<elt->nb;i++) {
    elt->current = i;
    struct nl_msg *msg2 = nlmsg_alloc();
    if (!msg2) return -2;
    genlmsg_put(msg2, NL_AUTO_PORT, NL_AUTO_SEQ,  g_netlink.id, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
    nla_put_u32(msg2, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nl_send_auto(g_netlink.socket, msg2);
    msg_received = false;
    while (!msg_received) nl_recvmsgs(g_netlink.socket, cb1);
    nlmsg_free(msg2);
  }

  return 0;
}

/******************************************************************************/
static uint8_t setraw(elt_t *elt, wfb_net_raw_t raw[]) {

  uint8_t ret = 0;
  uint16_t protocol = htons(ETH_P_ALL);

  for(uint8_t i=0;i<elt->nb;i++) {
    if (strcmp(elt->devs[i].drivername,drivername_arr[1])!=0) continue;
    strcpy(raw[ret].ifname, elt->devs[i].ifname);
    if (-1 == (raw[ret].fd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
    struct sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
    struct sock_fprog zero_program = { 1, &zero_bytecode};
    if (-1 == setsockopt(raw[ret].fd, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program, sizeof(zero_program))) exit(-1);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy( ifr.ifr_name, raw[ret].ifname, sizeof( ifr.ifr_name ) - 1 );
    if (ioctl( raw[ret].fd, SIOCGIFINDEX, &ifr ) < 0 ) exit(-1);
    struct sockaddr_ll sll;
    memset( &sll, 0, sizeof( sll ) );
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_protocol = protocol;
    if (-1 == bind(raw[ret].fd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1);
    char drain[1];
    while (recv(raw[ret].fd, drain, sizeof(drain), MSG_DONTWAIT) >= 0) {
      printf("----\n");
    };
    struct sock_filter full_bytecode = BPF_STMT(BPF_RET | BPF_K, (u_int)-1);
    struct sock_fprog full_program = { 1, &full_bytecode};
    if (-1 == setsockopt(raw[ret].fd, SOL_SOCKET, SO_ATTACH_FILTER, &full_program, sizeof(full_program))) ret=false;
    static const int32_t sock_qdisc_bypass = 1;
    if (-1 == setsockopt(raw[ret].fd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) ret=false;
    ret++;
  }
  return(ret);
}

/*****************************************************************************/
bool wfb_net_setfreq(int ifindex, uint32_t freq) {
  bool ret=true;
  struct nl_msg *msg=nlmsg_alloc();
  genlmsg_put(msg,0,0,g_netlink.id,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(msg,NL80211_ATTR_IFINDEX,ifindex);
  NLA_PUT_U32(msg,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(g_netlink.socket, msg) < 0) ret=false;
  nlmsg_free(msg);
  return(ret);
  nla_put_failure:
    nlmsg_free(msg);
    return(false);
}

/******************************************************************************/
void wfb_net_init(wfb_net_init_t *pnet) {

  g_netlink.socket = nl_socket_alloc();
  if (!g_netlink.socket) return -ENOMEM;
  nl_socket_set_buffer_size(g_netlink.socket, 8192, 8192);
  if (genl_connect(g_netlink.socket)) exit(-1);
  g_netlink.id = genl_ctrl_resolve(g_netlink.socket, "nl80211");
  if (g_netlink.id < 0) exit(-1);
  elt_t elt;
  memset(&elt,0,sizeof(elt));
  if (setwifi(&elt) < 0) exit(-1);
  if (elt.nb > 0) pnet->rawnb = setraw(&elt, pnet->raws);
  else pnet->rawnb = 0;
}
