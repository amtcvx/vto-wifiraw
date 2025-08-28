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

    struct iovec radiotaphd_rx_vec = { .iov_base = &praw->headsrx->radiotaphd_rx, 
                                       .iov_len = sizeof(praw->headsrx->radiotaphd_rx)};
    msg->headvecs.head[0] = radiotaphd_rx_vec;
    struct iovec ieeehd_rx_vec = { .iov_base = &praw->headsrx->ieeehd_rx, 
                                   .iov_len = sizeof(praw->headsrx->ieeehd_rx)};
    msg->headvecs.head[1] = ieeehd_rx_vec;

//    memset(ieeehd_rx_vec.iov_base, 0, ieeehd_rx_vec.iov_len);

  } else {

    struct iovec radiotaphd_tx_vec = { .iov_base = &praw->headstx->radiotaphd_tx, 
                                       .iov_len = praw->headstx->radiotaphd_tx_size};
    msg->headvecs.head[0] = radiotaphd_tx_vec;
    struct iovec ieeehd_tx_vec = { .iov_base = &praw->headstx->ieeehd_tx, 
                                   .iov_len = praw->headstx->ieeehd_tx_size};
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

  uint8_t template[]="devraw(%d) freqnb(%d) mainraw(%d) backraw(%d) incom(%d) fails(%d) sent(%d)\n";
  wfb_utils_log_t *plog = &pinit->log;
  for (uint8_t i=0; i < pinit->nbraws; i++) {
    wfb_net_status_t *pstat = &(pinit->rawdevs[i]->stat);
    plog->len += sprintf((char *)plog->txt + plog->len, (char *)template,
                          i, pstat->freqnb, pinit->rawchan.mainraw, pinit->rawchan.backraw, pstat->incoming, 
			  pstat->fails, pstat->sent);
  }
  if (pinit->nbraws == 0) plog->len += sprintf((char *)plog->txt + plog->len, "NO WIFI\n");
  sendto(plog->fd, plog->txt, plog->len, 0,  (const struct sockaddr *)&plog->addrout, sizeof(struct sockaddr));
  plog->len = 0;

}

/*****************************************************************************/
void setmainbackup(wfb_utils_init_t *pinit) {

#if BOARD
  for (uint8_t i=0; i < pinit->nbraws; i++) {
    wfb_net_status_t *pstat = &(pinit->rawdevs[i]->stat);
    if (pstat->fails != 0) {
      pstat->fails = 0;
      pstat->timecpt = 0;
      pstat->freqfree = false;
      if (i != pinit->rawchan.mainraw) {
        uint8_t nextfreqnb = 1 + pstat->freqnb;
        if (nextfreqnb > pinit->rawdevs[i]->nbfreqs) nextfreqnb = 0;
        pstat->freqnb = nextfreqnb;
        wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[i]->ifindex, pinit->rawdevs[i]->freqs[nextfreqnb]);
      }
    } else {
      if (pstat->timecpt < 10) pstat->timecpt++;
      else {
        pstat->timecpt = 0;
        pstat->freqfree = true;
      }
    }
  }
  if (pinit->rawchan.mainraw == -1) {
    for (uint8_t i=0; i < pinit->nbraws; i++) {
      if (pinit->rawdevs[i]->stat.freqfree) { pinit->rawchan.mainraw = i; break; }
    }
    for (uint8_t i=0; i < pinit->nbraws; i++) {
      if ((pinit->rawdevs[i]->stat.freqfree) && (i !=  pinit->rawchan.mainraw)) { pinit->rawchan.backraw = i; break; }
    }
  } else {
    if (!(pinit->rawdevs[pinit->rawchan.mainraw]->stat.freqfree)) {
      if ((pinit->rawchan.backraw > -1) && (pinit->rawdevs[pinit->rawchan.backraw]->stat.freqfree)) {
        pinit->rawchan.mainraw = pinit->rawchan.backraw;
      } else {
        for (uint8_t i=0; i < pinit->nbraws; i++) {
          if (pinit->rawdevs[i]->stat.freqfree) { pinit->rawchan.mainraw = i; break; }
	}
      }
    }
    if (pinit->rawchan.backraw == -1) {
      for (uint8_t i=0; i < pinit->nbraws; i++) { 
        if ((pinit->rawdevs[i]->stat.freqfree) && (i !=  pinit->rawchan.mainraw)) { pinit->rawchan.backraw = i; break; }
      }
    } else if (!(pinit->rawdevs[pinit->rawchan.backraw]->stat.freqfree)) pinit->rawchan.backraw = -1;
  }
  if (pinit->rawchan.mainraw != -1) {
    struct iovec *mainio =  &(pinit->downmsg.iov[pinit->rawchan.mainraw][0][0]);
    mainio->iov_len = sizeof(wfb_utils_down_t);
    if (pinit->rawchan.backraw == -1) {
      ((wfb_utils_down_t *)(mainio->iov_base))->chan = -1;
    } else { 
      struct iovec *backio =  &(pinit->downmsg.iov[pinit->rawchan.backraw][0][0]);
      ((wfb_utils_down_t *)mainio->iov_base)->chan = pinit->rawdevs[pinit->rawchan.backraw]->stat.freqnb;
      ((wfb_utils_down_t *)backio->iov_base)->chan = 100 + pinit->rawdevs[pinit->rawchan.mainraw]->stat.freqnb;
      backio->iov_len = sizeof(wfb_utils_down_t);
    }
  } 
#else
  for (uint8_t i=0; i < pinit->nbraws; i++) {
    wfb_net_status_t *pstat = &(pinit->rawdevs[i]->stat);
    if (pstat->timecpt < 1) pstat->timecpt++;
    else {
      pstat->timecpt = 0;
      if (pstat->incoming > 0) {
        pstat->incoming = 0;
	if (pstat->chan == -1) { pinit->rawchan.mainraw = i; }
	else {
          for (uint8_t j=0; j < pinit->nbraws; j++) {
            if (j!=i) {
	      if (pstat->chan < 100) { 
	        pinit->rawchan.mainraw = i; 
	        pinit->rawchan.backraw = j; 
	      } else {
                if (pstat->chan >= 100) { 
	          pinit->rawchan.mainraw = j; 
	          pinit->rawchan.backraw = i;
		}
	      }
              pinit->rawdevs[j]->stat.chan = pstat->chan; 
	      wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[j]->ifindex, 
			    pinit->rawdevs[j]->freqs[pinit->rawdevs[j]->stat.chan]);

	      break;
	    }
	  }
	}
      } else {
        uint8_t nextfreqnb = 1 + pstat->freqnb;
        if (nextfreqnb > pinit->rawdevs[i]->nbfreqs) nextfreqnb = 0;
        pstat->freqnb = nextfreqnb;
        wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[i]->ifindex, pinit->rawdevs[i]->freqs[nextfreqnb]);
      }
    }
  }
#endif // BOARD

}

/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *pinit) {
  printlog(pinit);
  setmainbackup(pinit);
}

/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *putils) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);

  putils->log.addrout.sin_family = AF_INET;
  putils->log.addrout.sin_port = htons(PORT_LOG);
  putils->log.addrout.sin_addr.s_addr = inet_addr(IP_LOCAL);
  putils->log.fd = socket(AF_INET, SOCK_DGRAM, 0);

  memset(&(putils->rawchan), -1, sizeof(wfb_utils_rawchan_t));

  wfb_net_init_t net;
  memset(&net,0,sizeof(wfb_net_init_t));
  wfb_net_init(&net);

  putils->nbdev = MAXDEV;

  static wfb_utils_heads_rx_t heads_rx;
  putils->raws.headsrx = &heads_rx;

  putils->raws.headstx = net.headstx;
  putils->raws.rawmsgcurr = 0;

  putils->sockidnl = net.sockidnl;

  putils->fd[0] = timerfd_create(CLOCK_MONOTONIC, 0);
  putils->readsets[0].fd = putils->fd[0];
  putils->readsets[0].events = POLLIN;
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(putils->fd[0], 0, &period, NULL);

  putils->readtabnb = 1;
  for(uint8_t i=0;i<net.nbraws;i++) {
    uint8_t nextfreqnb = i * (uint8_t) ((net.rawdevs[i]->nbfreqs) / net.nbraws);

    if (!(wfb_net_setfreq(putils->sockidnl, net.rawdevs[i]->ifindex, net.rawdevs[i]->freqs[nextfreqnb]))) continue;
    net.rawdevs[i]->stat.freqnb = nextfreqnb;
    net.rawdevs[i]->stat.freqfree = false;

    putils->fd[putils->readtabnb]  = net.rawdevs[i]->sockfd;

    putils->rawdevs[(putils->readtabnb) - 1] = net.rawdevs[i];

    putils->readsets[putils->readtabnb].fd = putils->fd[putils->readtabnb];
    putils->readsets[putils->readtabnb].events = POLLIN;
    (putils->readtabnb) += 1;
  }
  putils->nbraws = putils->readtabnb - 1;


  for (uint8_t i=0; i < MAXRAWDEV; i++) {
    for (uint8_t j=0; j < MAXMSG; j++) {
      for (uint8_t k=0; k < FEC_N; k++) {
        putils->downmsg.iov[i][j][k].iov_base = &(putils->downmsg.buf[i][j][k][0]);
      }
    }
  }

}
