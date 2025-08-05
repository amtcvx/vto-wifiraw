#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <netinet/in.h>

#include "wfb_utils.h"
#include "wfb_net.h"

#if RAW
extern uint8_t wfb_net_ieeehd_tx[];
extern uint8_t wfb_net_ieeehd_rx[24];
extern uint8_t wfb_net_radiotaphd_tx[];
extern uint8_t wfb_net_radiotaphd_rx[35];
#endif // RAW

/*****************************************************************************/
void wfb_utils_presetrawmsg(wfb_utils_raw_t *raw, bool rxflag) {
  wfb_utils_pay_t *pay = raw->pay;
  wfb_utils_rawmsg_t *msg = raw->rawmsg;
  struct iovec pay_vec = { .iov_base = &pay, .iov_len = sizeof(wfb_utils_pay_t)};

  memset(&msg->bufs, 0, sizeof(msg->bufs));
  msg->iovecs.iov_base = &msg->bufs;
  msg->iovecs.iov_len  = ONLINE_MTU;
#if RAW
  if (rxflag) {
    struct iovec wfb_net_radiotaphd_rx_vec = { .iov_base = &wfb_net_radiotaphd_rx, .iov_len = sizeof(wfb_net_radiotaphd_rx)};
    msg->headvecs.head[0] = wfb_net_radiotaphd_rx_vec;
    struct iovec wfb_net_ieeehd_rx_vec = { .iov_base = &wfb_net_ieeehd_rx, .iov_len = sizeof(wfb_net_ieeehd_rx)};
    msg->headvecs.head[1] = wfb_net_ieeehd_rx_vec;
    memset(wfb_net_ieeehd_rx_vec.iov_base, 0, wfb_net_ieeehd_rx_vec.iov_len);
  } else {
    struct iovec wfb_net_radiotaphd_tx_vec = { .iov_base = &wfb_net_radiotaphd_tx, .iov_len = sizeof(wfb_net_radiotaphd_tx)};
    msg->headvecs.head[0] = wfb_net_radiotaphd_tx_vec;
    struct iovec wfb_net_ieeehd_tx_vec = { .iov_base = &wfb_net_ieeehd_tx, .iov_len = sizeof(wfb_net_ieeehd_tx)};
    msg->headvecs.head[1] = wfb_net_ieeehd_tx_vec;
  }
  memset(pay_vec.iov_base, 0, pay_vec.iov_len);
  msg->headvecs.head[2] = pay_vec;
  msg->headvecs.head[3] = msg->iovecs;
  msg->msg.msg_iovlen = 4;
#else
  msg->headvecs.head[0] = pay_vec;
  msg->headvecs.head[1] = msg->iovecs;
  msg->msg.msg_iovlen = 2;
#endif // RAW
  msg->msg.msg_iov = &msg->headvecs.head[0];
}


/*****************************************************************************/
void printlog(wfb_utils_log_t *stat) {
  uint8_t template[]="devraw(%d) fails(%d) incom(%d)\n";
/*
  for (uint8_t i=0; i<g_net.rawnb; i++) {
    g_log.len += sprintf((char *)g_log.txt + g_log.len, (char *)template,
		  g_net.raws[i].fd, g_net.raws[i].fails, g_net.raws[i].incoming);
  }
*/
  stat->len += sprintf((char *)stat->txt, "TIC\n");
  sendto(stat->fd, stat->txt, stat->len, 0, (const struct sockaddr *)&stat->addrout, sizeof(struct sockaddr));
  stat->len = 0;

}

/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_log_t *stat) {
  printlog(stat);
}


/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *putils) {

  static wfb_utils_log_t stat;
  stat.addrout.sin_family = AF_INET;
  stat.addrout.sin_port = htons(PORT_LOG);
  stat.addrout.sin_addr.s_addr = inet_addr(IP_LOCAL);
  stat.fd = socket(AF_INET, SOCK_DGRAM, 0);

  wfb_net_init_t net;
  memset(&net,0,sizeof(wfb_net_init_t));
  wfb_net_init(&net);

  putils->nbdev = MAXDEV;
  putils->rawlimit = 1 + net.nbraws;
  putils->readtabnb = 0;
  putils->stat = &stat;
  putils->raws = net.rawdevs;

  uint8_t devcpt = 0;
  putils->fd[devcpt] = timerfd_create(CLOCK_MONOTONIC, 0);
  putils->readsets[putils->readtabnb].fd = putils->fd[devcpt];
  putils->readsets[putils->readtabnb].events = POLLIN;
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(putils->fd[devcpt], 0, &period, NULL);
  putils->readtab[putils->readtabnb] = devcpt;
  (putils->readtabnb) += 1;

  for(uint8_t i=0;i<net.nbraws;i++) {
    devcpt = 1 + i;
    putils->fd[devcpt]  = net.rawdevs[i]->sockfd;
    putils->readsets[putils->readtabnb].fd = putils->fd[devcpt];
    putils->readsets[putils->readtabnb].events = POLLIN;
    putils->readtab[putils->readtabnb] = devcpt;
    (putils->readtabnb) += 1;
  }
}
