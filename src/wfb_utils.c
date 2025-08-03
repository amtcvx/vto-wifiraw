#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "wfb_utils.h"
#include "wfb_net.h"

#if RAW
extern struct iovec wfb_net_ieeehd_tx_vec;
extern struct iovec wfb_net_ieeehd_rx_vec;
extern struct iovec wfb_net_radiotaphd_tx_vec;
extern struct iovec wfb_net_radiotaphd_rx_vec;
#endif // RAW

wfb_utils_pay_t wfb_utils_pay;
struct iovec wfb_utils_pay_vec = { .iov_base = &wfb_utils_pay, .iov_len = sizeof(wfb_utils_pay_t)};

struct log_t {
  uint8_t fd;
  struct sockaddr_in addrout;
  uint8_t txt[1000];
  uint16_t len;
} g_log;

wfb_net_init_t g_net;

/*****************************************************************************/
void wfb_utils_presetrawmsg(wfb_utils_rawmsg_t *msg, bool rxflag) {

  memset(&msg->bufs, 0, sizeof(msg->bufs));
  msg->iovecs.iov_base = &msg->bufs;
  msg->iovecs.iov_len  = ONLINE_MTU;
#if RAW
  if (rxflag) {
    msg->headvecs.head[0] = wfb_net_radiotaphd_rx_vec;
    msg->headvecs.head[1] = wfb_net_ieeehd_rx_vec;
    memset(wfb_net_ieeehd_rx_vec.iov_base, 0, wfb_net_ieeehd_rx_vec.iov_len);
  } else {
    msg->headvecs.head[0] = wfb_net_radiotaphd_tx_vec;
    msg->headvecs.head[1] = wfb_net_ieeehd_tx_vec;
  }
  memset(wfb_utils_pay_vec.iov_base, 0, wfb_utils_pay_vec.iov_len);
  msg->headvecs.head[2] = wfb_utils_pay_vec;
  msg->headvecs.head[3] = msg->iovecs;
  msg->msg.msg_iovlen = 4;
#else
  msg->headvecs.head[0] = wfb_utils_pay_vec;
  msg->headvecs.head[1] = msg->iovecs;
  msg->msg.msg_iovlen = 2;
#endif // RAW
  msg->msg.msg_iov = &msg->headvecs.head[0];
}


/*****************************************************************************/
void printlog() {
  uint8_t template[]="devraw(%d) fails(%d) incom(%d)\n";

  for (uint8_t i=0; i<g_net.rawnb; i++) {
    g_log.len += sprintf((char *)g_log.txt + g_log.len, (char *)template,
		  g_net.raws[i].fd, g_net.raws[i].fails, g_net.raws[i].incoming);
  }

  sendto(g_log.fd, g_log.txt, g_log.len, 0, (const struct sockaddr *)&g_log.addrout, sizeof(struct sockaddr));
  g_log.len = 0;
}

/*****************************************************************************/
void wfb_utils_periodic() {
  printlog();
}


/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *putils) {

  g_log.addrout.sin_family = AF_INET;
  g_log.addrout.sin_port = htons(PORT_LOG);
  g_log.addrout.sin_addr.s_addr = inet_addr(IP_LOCAL);
  g_log.fd = socket(AF_INET, SOCK_DGRAM, 0);

  wfb_net_init(&g_net);

  putils->nbdev = MAXDEV;
  putils->rawlimit = 1 + g_net.rawnb;
  putils->readtabnb = 0;
  putils->pnet = &g_net;

  uint8_t devcpt = 0;
  putils->fd[devcpt] = timerfd_create(CLOCK_MONOTONIC, 0);
  putils->readsets[putils->readtabnb].fd = putils->fd[devcpt];
  putils->readsets[putils->readtabnb].events = POLLIN;
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(putils->fd[devcpt], 0, &period, NULL);
  putils->readtab[putils->readtabnb] = devcpt;
  (putils->readtabnb) += 1;

  for(uint8_t i=0;i<(g_net.rawnb);i++) {
    devcpt = 1 + i;
    putils->fd[devcpt]  = g_net.raws[i].fd;
    putils->readsets[putils->readtabnb].fd = putils->fd[devcpt];
    putils->readsets[putils->readtabnb].events = POLLIN;
    putils->readtab[putils->readtabnb] = devcpt;
    (putils->readtabnb) += 1;
  }
}
