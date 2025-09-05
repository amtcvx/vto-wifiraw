#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <netinet/in.h>

#include <fcntl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

#include "wfb_utils.h"
#include "wfb_net.h"
#include "zfex.h"

#define TUN_MTU 1400

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
      if (((pinit->nbraws == 1) && (-1 == pinit->rawchan.mainraw))
        || (pinit->nbraws > 1)) {

        pinit->msgout.eltout[i].iov[WFB_PRO].iov_len = 0;
	if (i == pinit->rawchan.mainraw) pinit->rawchan.mainraw = -1;
	if (i == pinit->rawchan.backraw) pinit->rawchan.backraw = -1;

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
    if (pinit->rawchan.backraw != -1) {
      pinit->rawchan.mainraw = pinit->rawchan.backraw;
      pinit->rawchan.backraw = -1;
    } else { 
      for (uint8_t i=0; i < pinit->nbraws; i++) {
        if (pinit->rawdevs[i]->stat.freqfree) { pinit->rawchan.mainraw = i; break; }
      }
    }
  }

  if (pinit->rawchan.mainraw != -1) {
    for (uint8_t i=0; i < pinit->nbraws; i++) {
      if ((pinit->rawdevs[i]->stat.freqfree) && (i !=  pinit->rawchan.mainraw)) { pinit->rawchan.backraw = i; break; }
    }
  }

  if (pinit->rawchan.mainraw != -1) {
    pinit->msgout.eltout[pinit->rawchan.mainraw].iov[WFB_PRO].iov_len = sizeof(wfb_utils_pro_t);
    if (pinit->rawchan.backraw == -1) {
       ((wfb_utils_pro_t *)pinit->msgout.eltout[pinit->rawchan.mainraw].iov[WFB_PRO].iov_base)->chan = -1;
    } else { 
      ((wfb_utils_pro_t *)pinit->msgout.eltout[pinit->rawchan.mainraw].buf_pro)->chan = pinit->rawdevs[pinit->rawchan.backraw]->stat.freqnb;
      ((wfb_utils_pro_t *)pinit->msgout.eltout[pinit->rawchan.backraw].buf_pro)->chan = 100 + pinit->rawdevs[pinit->rawchan.mainraw]->stat.freqnb;
      pinit->msgout.eltout[pinit->rawchan.backraw].iov[WFB_PRO].iov_len = sizeof(wfb_utils_pro_t);
    }
  } 
#else
  for (uint8_t i=0; i < pinit->nbraws; i++) {
    wfb_net_status_t *pstat = &(pinit->rawdevs[i]->stat);
    if (pstat->incoming > 0) { 
      pstat->incoming = 0;
      pstat->timecpt = 0;
      pstat->freqfree = false;

    } else {
      if (pstat->timecpt < 3) pstat->timecpt++;
      else {
        pstat->timecpt = 0;
        pstat->freqfree = true;

	if (i == pinit->rawchan.mainraw) pinit->rawchan.mainraw = -1;
	if (i == pinit->rawchan.backraw) pinit->rawchan.backraw = -1;
     
        uint8_t nextfreqnb = 1 + pstat->freqnb;
        if (nextfreqnb > pinit->rawdevs[i]->nbfreqs) nextfreqnb = 0;
        pstat->freqnb = nextfreqnb;
        wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[i]->ifindex, pinit->rawdevs[i]->freqs[nextfreqnb]);
      }
    }
  }

  for (uint8_t i=0; i < pinit->nbraws; i++) {
    if (!(pinit->rawdevs[i]->stat.freqfree)) {
      if (pinit->rawdevs[i]->stat.chan == -1) { 
        pinit->rawchan.mainraw = i; 
	pinit->rawchan.backraw = -1; 
	printf("A (%d)\n",i);
        break; // for i
      } else if (pinit->rawdevs[i]->stat.chan < 100) { 
        pinit->rawchan.mainraw = i; 
	printf("B (%d)\n",i);
        for (uint8_t j=0; j < pinit->nbraws; j++) {
          if (j != pinit->rawchan.mainraw) {
            pinit->rawchan.backraw = j;
	    printf("B1 (%d)\n",j);
	    if (pinit->rawdevs[j]->stat.freqnb !=  pinit->rawdevs[i]->stat.chan) {
              pinit->rawdevs[j]->stat.freqnb = pinit->rawdevs[i]->stat.chan;
              wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[j]->ifindex, pinit->rawdevs[j]->freqs[ pinit->rawdevs[j]->stat.freqnb ]);
	      printf("B2 (%d)(%d)\n",j,pinit->rawdevs[j]->stat.freqnb);
	      break; // for j
	    }
	  }
	}
	break; // for i
      } else if (pinit->rawdevs[i]->stat.chan >= 100) { 
        printf("C (%d)\n",i);
        pinit->rawchan.backraw = i; 
        for (uint8_t j=0; j < pinit->nbraws; j++) {
          if (j != pinit->rawchan.backraw)  {
            pinit->rawchan.mainraw = j;
	    printf("C1 (%d)\n",j);
	    if (pinit->rawdevs[j]->stat.freqnb !=  (pinit->rawdevs[i]->stat.chan - 100)) {
              pinit->rawdevs[j]->stat.freqnb = pinit->rawdevs[i]->stat.chan - 100;
              wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[j]->ifindex, pinit->rawdevs[j]->freqs[ pinit->rawdevs[j]->stat.freqnb ]);
	      printf("C2 (%d)(%d)\n",j,pinit->rawdevs[j]->stat.freqnb);
	      break; // for j
	    }
	  }
	}
        break; // for i
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
void build_tun(uint8_t *fd) {
  uint16_t fd_tun_udp;
  struct ifreq ifr;

  memset(&ifr, 0, sizeof(struct ifreq));
  struct sockaddr_in addr, dstaddr;
#if BOARD
  strcpy(ifr.ifr_name,"airtun");
  addr.sin_addr.s_addr = inet_addr(TUNIP_BOARD);
  dstaddr.sin_addr.s_addr = inet_addr(TUNIP_GROUND);
#else
  strcpy(ifr.ifr_name, "grdtun");
  addr.sin_addr.s_addr = inet_addr(TUNIP_GROUND);
  dstaddr.sin_addr.s_addr = inet_addr(TUNIP_BOARD);
#endif // BOARD
  if (0 > (*fd = open("/dev/net/tun",O_RDWR))) exit(-1);
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (ioctl(*fd, TUNSETIFF, &ifr ) < 0 ) exit(-1);
  if (-1 == (fd_tun_udp = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP))) exit(-1);
  addr.sin_family = AF_INET;
  memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));
  if (ioctl( fd_tun_udp, SIOCSIFADDR, &ifr ) < 0 ) exit(-1);
  addr.sin_addr.s_addr = inet_addr(IPBROAD);
  memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));
  if (ioctl( fd_tun_udp, SIOCSIFNETMASK, &ifr ) < 0 ) exit(-1);
  dstaddr.sin_family = AF_INET;
  memcpy(&ifr.ifr_addr, &dstaddr, sizeof(struct sockaddr));
  if (ioctl( fd_tun_udp, SIOCSIFDSTADDR, &ifr ) < 0 ) exit(-1);
  ifr.ifr_mtu = TUN_MTU;
  if (ioctl( fd_tun_udp, SIOCSIFMTU, &ifr ) < 0 ) exit(-1);
  ifr.ifr_flags = IFF_UP ;
  if (ioctl( fd_tun_udp, SIOCSIFFLAGS, &ifr ) < 0 ) exit(-1);
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
#if BOARD
    net.rawdevs[i]->stat.freqfree = false;
#else
    net.rawdevs[i]->stat.freqfree = true;
#endif // BOARD
    putils->fd[putils->readtabnb]  = net.rawdevs[i]->sockfd;
    putils->rawdevs[(putils->readtabnb) - 1] = net.rawdevs[i];
    putils->readsets[putils->readtabnb].fd = putils->fd[putils->readtabnb];
    putils->readsets[putils->readtabnb].events = POLLIN;
    (putils->readtabnb) += 1;
  }
  putils->nbraws = putils->readtabnb - 1;

  build_tun(&putils->fd[putils->readtabnb]); // One bidirectional link
  putils->readsets[putils->readtabnb].fd = putils->fd[putils->readtabnb];
  putils->readsets[putils->readtabnb].events = POLLIN;
  (putils->readtabnb) += 1;


  for (uint8_t i=0; i < MAXRAWDEV; i++) {
    putils->msgin.eltin[i].curr = 0;
    for (uint8_t k=0; k < FEC_N; k++) {
      putils->msgin.eltin[i].iov[k].iov_base = &putils->msgin.eltin[i].buf_raw[k];
      putils->msgin.eltin[i].iov[k].iov_len = ONLINE_MTU;
    }
    for (uint8_t j=0; j < WFB_NB; j++) {
      if (j == WFB_VID) {
        for (uint8_t k=0; k < FEC_N; k++) {
	  putils->msgout.eltout[i].buf_fec[k].iovvid.iov_base = &putils->msgout.eltout[i].buf_fec[k].buf_vid;
	  putils->msgout.eltout[i].buf_fec[k].iovvid.iov_len = 0;
	}
      } else {
        struct iovec *piov = &putils->msgout.eltout[i].iov[j];
        piov->iov_len = 0;
        if (j == WFB_PRO) piov->iov_base = &putils->msgout.eltout[i].buf_pro;
        else if (j == WFB_TUN) piov->iov_base = &putils->msgout.eltout[i].buf_tun;
        else if (j == WFB_TEL) piov->iov_base = &putils->msgout.eltout[i].buf_tel;
      }
    }
  }
}
