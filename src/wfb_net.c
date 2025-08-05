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

typedef struct {
  uint8_t nb;
  uint8_t curr;
  wfb_net_device_t *devs;
} elt_t;

const char * drivername_arr[] =  { "rt2800usb", "rtl88XXau", "rtw_8822bu", "rtw_8821ce" };

/******************************************************************************/
static int finish_callback(struct nl_msg *msg, void *arg) {
  bool* finished = arg;
  *finished = true;
  return NL_SKIP;
}

/******************************************************************************/
static int getallinterfaces_callback(struct nl_msg *msg, void *arg) {

  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
  nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

  wfb_net_device_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->nb]);
  ((((elt_t *)arg)->nb)++);

  if (tb_msg[NL80211_ATTR_IFNAME])  strcpy(ptr->ifname, nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
  if (tb_msg[NL80211_ATTR_IFINDEX]) ptr->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
  if (tb_msg[NL80211_ATTR_IFTYPE])  ptr->iftype = nla_get_u32(tb_msg[NL80211_ATTR_IFTYPE]);

  return NL_SKIP;
}

/******************************************************************************/
static int getsinglewifi_callback(struct nl_msg *msg, void *arg) {

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

    wfb_net_device_t *ptr = &(((elt_t *)arg)->devs[((elt_t *)arg)->curr]);

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
static uint8_t setwifi(uint8_t sockid, struct nl_sock *socknl, struct nl_sock *sockrt, elt_t *elt) {

  bool msg_received = false;

  struct nl_cb *cb1 = nl_cb_alloc(NL_CB_DEFAULT);
  if (!cb1) return(0);
  nl_cb_set(cb1, NL_CB_VALID, NL_CB_CUSTOM, getallinterfaces_callback, elt);
  nl_cb_set(cb1, NL_CB_FINISH, NL_CB_CUSTOM, finish_callback, &msg_received);

  struct nl_msg *msg1 = nlmsg_alloc();
  if (!msg1) return(0);
  genlmsg_put(msg1, NL_AUTO_PORT, NL_AUTO_SEQ, sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
  nl_send_auto(socknl, msg1);
  msg_received = false;
  while (!msg_received) nl_recvmsgs(socknl, cb1);
  nlmsg_free(msg1);

  if (elt->nb == 0) return(0);

  for(uint8_t i=0;i<elt->nb;i++) {
    struct nl_msg *msg3 = nlmsg_alloc();
    if (!msg3) return(0);
    genlmsg_put(msg3,0,0,sockid,0,0,NL80211_CMD_SET_INTERFACE,0);  //  DOWN interfaces
    nla_put_u32(msg3, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nla_put_u32(msg3, NL80211_ATTR_IFTYPE,NL80211_IFTYPE_MONITOR);
    nl_send_auto(socknl, msg3);
    if (nl_send_auto(socknl, msg3) >= 0)  nl_recvmsgs_default(socknl);
    nlmsg_free(msg3);
  }

  elt->nb = 0;
  struct nl_msg *msg4 = nlmsg_alloc();
  if (!msg4) return(0);
  genlmsg_put(msg4, NL_AUTO_PORT, NL_AUTO_SEQ, sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
  nl_send_auto(socknl, msg4);
  msg_received = false;
  while (!msg_received) nl_recvmsgs(socknl, cb1);
  nlmsg_free(msg4);

  unblock_rfkill(elt);

  int8_t err = 0;
  struct nl_cache *cache;
  struct rtnl_link *link, *change;
  if ((err = rtnl_link_alloc_cache(sockrt, AF_UNSPEC, &cache)) >= 0) {
    for(uint8_t i=0;i<elt->nb;i++) {
      if ((link = rtnl_link_get(cache,elt->devs[i].ifindex))) {
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
    elt->curr = i;
    struct nl_msg *msg2 = nlmsg_alloc();
    if (!msg2) return(0);
    genlmsg_put(msg2, NL_AUTO_PORT, NL_AUTO_SEQ, sockid, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
    nla_put_u32(msg2, NL80211_ATTR_IFINDEX, elt->devs[i].ifindex);
    nl_send_auto(socknl, msg2);
    msg_received = false;
    while (!msg_received) nl_recvmsgs(socknl, cb1);
    nlmsg_free(msg2);
  }

  return 0;
}

/******************************************************************************/
static uint8_t setraw(elt_t *elt, wfb_net_device_t *arr[]) {

  uint8_t cpt = 0;
  uint16_t protocol = htons(ETH_P_ALL);

  for(uint8_t i=0;i<elt->nb;i++) {
//    if (strcmp(elt->devs[i].drivername,drivername_arr[1])!=0) continue;
    if (-1 == (elt->devs[i].sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) continue;
    struct sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
    struct sock_fprog zero_program = { 1, &zero_bytecode};
    if (-1 == setsockopt(elt->devs[i].sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program, sizeof(zero_program))) continue;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy( ifr.ifr_name, elt->devs[i].ifname, sizeof( ifr.ifr_name ) - 1 );
    if (ioctl( elt->devs[i].sockfd, SIOCGIFINDEX, &ifr ) < 0 ) continue;
    struct sockaddr_ll sll;
    memset( &sll, 0, sizeof( sll ) );
    sll.sll_family   = AF_PACKET;
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_protocol = protocol;
    if (-1 == bind(elt->devs[i].sockfd, (struct sockaddr *)&sll, sizeof(sll))) continue;
    char drain[1];
    while (recv(elt->devs[i].sockfd, drain, sizeof(drain), MSG_DONTWAIT) >= 0) {
      printf("----\n");
    };
    struct sock_filter full_bytecode = BPF_STMT(BPF_RET | BPF_K, (u_int)-1);
    struct sock_fprog full_program = { 1, &full_bytecode};
    if (-1 == setsockopt(elt->devs[i].sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &full_program, sizeof(full_program))) continue;
    static const int32_t sock_qdisc_bypass = 1;
    if (-1 == setsockopt(elt->devs[i].sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) continue;

    arr[cpt] = &(elt->devs[i]);
    cpt++; 
  }
  return(cpt);
}

/*****************************************************************************/
bool wfb_net_setfreq(uint8_t sockid, struct nl_sock *sockrt, int ifindex, uint32_t freq) {
  bool ret=true;
  struct nl_msg *msg=nlmsg_alloc();
  genlmsg_put(msg,0,0,sockid,0,0,NL80211_CMD_SET_CHANNEL,0);
  NLA_PUT_U32(msg,NL80211_ATTR_IFINDEX,ifindex);
  NLA_PUT_U32(msg,NL80211_ATTR_WIPHY_FREQ,freq);
  if (nl_send_auto(sockrt, msg) < 0) ret=false;
  nlmsg_free(msg);
  return(ret);
  nla_put_failure:
    nlmsg_free(msg);
    return(false);
}

/******************************************************************************/
bool wfb_net_init(wfb_net_init_t *pnet) {

  struct nl_sock *socknl = nl_socket_alloc();
  if (!socknl) return(false);
  nl_socket_set_buffer_size(socknl, 8192, 8192);
  if (genl_connect(socknl)) return(false);

  static int8_t sockid;
  sockid = genl_ctrl_resolve(socknl, "nl80211");
  if (sockid < 0) return(false);

  static struct nl_sock *sockrt;
  sockrt = nl_socket_alloc();
  if (!sockrt) return(false);
  if (nl_connect(sockrt, NETLINK_ROUTE)) return(false);

  static wfb_net_device_t wfb_net_all80211[MAXRAWDEV];
  elt_t elt;
  memset(&elt, 0, sizeof(elt_t));
  elt.devs = wfb_net_all80211;

  static uint8_t nb;
  if ((nb = setwifi(sockid, socknl, sockrt, &elt)) >= 0) {
    static wfb_net_device_t *wfb_net_raw[MAXRAWDEV];
    nb = setraw(&elt, wfb_net_raw);
    if (nb > 0) { pnet->sockid = sockid; pnet->sockrt = sockrt; pnet->nbraws = nb; 
	          memcpy(&(pnet->rawdevs), &wfb_net_raw, nb*sizeof(wfb_net_device_t *)); 
		  return(true); }
  }

  return(false);
}
