#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <netinet/in.h>

#include "wfb_utils.h"
#include "wfb_net.h"
#include "zfex.h"


/*****************************************************************************/
void wfb_utils_presetrawmsg(wfb_utils_raw_t *praw, bool rxflag) {

  wfb_utils_rawmsg_t *msg = &(praw->rawmsg[praw->rawmsgcurr]);
  memset(&msg->bufs, 0, sizeof(msg->bufs));
  msg->iovecs.iov_base = &msg->bufs;
  msg->iovecs.iov_len  = ONLINE_MTU;

#if RAW
  if (rxflag) {

    struct iovec radiotaphd_rx_vec = { .iov_base = &praw->headsrx->radiotaphd_rx, .iov_len = sizeof(praw->headsrx->radiotaphd_rx)};
    msg->headvecs.head[0] = radiotaphd_rx_vec;
    struct iovec ieeehd_rx_vec = { .iov_base = &praw->headsrx->ieeehd_rx, .iov_len = sizeof(praw->headsrx->ieeehd_rx)};
    msg->headvecs.head[1] = ieeehd_rx_vec;
    memset(ieeehd_rx_vec.iov_base, 0, ieeehd_rx_vec.iov_len);

  } else {

    struct iovec radiotaphd_tx_vec = { .iov_base = &praw->headstx->radiotaphd_tx, .iov_len = praw->headstx->radiotaphd_tx_size};
    msg->headvecs.head[0] = radiotaphd_tx_vec;
    struct iovec ieeehd_tx_vec = { .iov_base = &praw->headstx->ieeehd_tx, .iov_len = praw->headstx->ieeehd_tx_size};
    msg->headvecs.head[1] = ieeehd_tx_vec;
  }

  wfb_utils_pay_t *pay = &(praw->pay);
  struct iovec pay_vec = { .iov_base = &pay, .iov_len = sizeof(wfb_utils_pay_t)};
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
void printlog(wfb_utils_init_t *pinit) {
  uint8_t template[]="devraw(%d) incom(%d) fails(%d)\n";

  wfb_net_device_t *rawdevs; 
  wfb_utils_log_t *pstat = &pinit->stat;
  for (uint8_t i=0; i < pinit->nbraws; i++) {
    rawdevs = &pinit->rawdevs[i];
    pstat->len += sprintf((char *)pstat->txt + pstat->len, (char *)template,
		  i, rawdevs->incoming, rawdevs->fails);
  }
  sendto(pstat->fd, pstat->txt, pstat->len, 0, (const struct sockaddr *)&pstat->addrout, sizeof(struct sockaddr));
  pstat->len = 0;

}

/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *pinit) {
  printlog(pinit);
}


/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *putils) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);

  putils->stat.addrout.sin_family = AF_INET;
  putils->stat.addrout.sin_port = htons(PORT_LOG);
  putils->stat.addrout.sin_addr.s_addr = inet_addr(IP_LOCAL);
  putils->stat.fd = socket(AF_INET, SOCK_DGRAM, 0);

  wfb_net_init_t net;
  memset(&net,0,sizeof(wfb_net_init_t));
  wfb_net_init(&net);

  putils->nbdev = MAXDEV;
  putils->nbraws = net.nbraws;
  putils->rawlimit = 1 + net.nbraws;
  putils->readtabnb = 0;

  putils->raws.headstx = net.headstx;
  putils->raws.rawmsgcurr = 0;

  putils->sockidnl = net.sockidnl;
  putils->rawdevs = *net.rawdevs;

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
