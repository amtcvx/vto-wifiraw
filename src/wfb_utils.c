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
      if (i != pinit->rawchan.mainraw) {
        uint8_t nextfreqnb = 1 + pinit->rawdevs[i]->stat.freqnb;
        if (nextfreqnb > pinit->rawdevs[i]->nbfreqs) nextfreqnb = 0;
        pinit->rawdevs[i]->stat.freqnb = nextfreqnb;
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

  if (pinit->nbraws == 1) {
    if (pinit->rawchan.mainraw == -1) {
      for (uint8_t i=0; i < pinit->nbraws; i++) {
        if (pinit->rawdevs[i]->stat.freqfree) { pinit->rawchan.mainraw = i; break; }
      }
    }
  } else {
	  
    if (pinit->rawchan.mainraw != -1) {
      if (!(pinit->rawdevs[pinit->rawchan.mainraw]->stat.freqfree)) {
        pinit->rawchan.mainraw = -1;
        if (pinit->rawchan.backraw != -1) {
          if (pinit->rawdevs[pinit->rawchan.backraw]->stat.freqfree) {
            pinit->rawchan.mainraw = pinit->rawchan.backraw;
	    pinit->rawchan.backraw = -1;
	  }
        } else {
          for (uint8_t i=0; i < pinit->nbraws; i++) {
            if (pinit->rawdevs[i]->stat.freqfree) { pinit->rawchan.mainraw = i; break; }
	  }
        }
      }
    } else {
      for (uint8_t i=0; i < pinit->nbraws; i++) {
        if (pinit->rawdevs[i]->stat.freqfree) { pinit->rawchan.mainraw = i; break; }
      }
    }

    if (pinit->rawchan.backraw != -1) {
      if  (!(pinit->rawdevs[pinit->rawchan.backraw]->stat.freqfree)) pinit->rawchan.backraw = -1;
    }
    if ((pinit->rawchan.mainraw != -1) && (pinit->rawchan.backraw == -1)) {
      for (uint8_t i=0; i < pinit->nbraws; i++) {
        if ((i != pinit->rawchan.mainraw) && (pinit->rawdevs[i]->stat.freqfree)) { pinit->rawchan.backraw = i; break; }
      }
    }
  }


  if (pinit->rawchan.mainraw != -1) {
    pinit->msgout.iov[WFB_PRO][pinit->rawchan.mainraw][0].iov_len = sizeof(wfb_utils_pro_t);
    if (pinit->rawchan.backraw == -1) {
       ((wfb_utils_pro_t *)pinit->msgout.iov[WFB_PRO][pinit->rawchan.mainraw][0].iov_base)->chan = -1;
    } else { 
      ((wfb_utils_pro_t *)pinit->msgout.iov[WFB_PRO][pinit->rawchan.mainraw][0].iov_base)->chan = pinit->rawdevs[pinit->rawchan.backraw]->stat.freqnb;
      ((wfb_utils_pro_t *)pinit->msgout.iov[WFB_PRO][pinit->rawchan.backraw][0].iov_base)->chan = 100 + pinit->rawdevs[pinit->rawchan.mainraw]->stat.freqnb;
      pinit->msgout.iov[WFB_PRO][pinit->rawchan.backraw][0].iov_len = sizeof(wfb_utils_pro_t);
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
        break; // for i
      } else if (pinit->rawdevs[i]->stat.chan < 100) { 
        pinit->rawchan.mainraw = i; 
        for (uint8_t j=0; j < pinit->nbraws; j++) {
          if (j != pinit->rawchan.mainraw) {
            pinit->rawchan.backraw = j;
	    if (pinit->rawdevs[j]->stat.freqnb !=  pinit->rawdevs[i]->stat.chan) {
              pinit->rawdevs[j]->stat.freqnb = pinit->rawdevs[i]->stat.chan;
              wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[j]->ifindex, pinit->rawdevs[j]->freqs[ pinit->rawdevs[j]->stat.freqnb ]);
	      break; // for j
	    }
	  }
	}
	break; // for i
      } else if (pinit->rawdevs[i]->stat.chan >= 100) { 
        pinit->rawchan.backraw = i; 
        for (uint8_t j=0; j < pinit->nbraws; j++) {
          if (j != pinit->rawchan.backraw)  {
            pinit->rawchan.mainraw = j;
	    if (pinit->rawdevs[j]->stat.freqnb !=  (pinit->rawdevs[i]->stat.chan - 100)) {
              pinit->rawdevs[j]->stat.freqnb = pinit->rawdevs[i]->stat.chan - 100;
              wfb_net_setfreq(pinit->sockidnl, pinit->rawdevs[j]->ifindex, pinit->rawdevs[j]->freqs[ pinit->rawdevs[j]->stat.freqnb ]);
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
#if RAW
  printlog(pinit);
  setmainbackup(pinit);
#else  // RAW
  pinit->rawchan.mainraw = 0;
#endif  // RAW
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

  fec_new(FEC_K, FEC_N, &(putils->fec_p));

  memset(&(putils->rawchan), -1, sizeof(wfb_utils_rawchan_t));

  static wfb_utils_heads_rx_t heads_rx;
  putils->raws.headsrx = &heads_rx;

  putils->nbdev = MAXDEV;

#if RAW
  wfb_net_init_t net;
  memset(&net,0,sizeof(wfb_net_init_t));
  wfb_net_init(&net);
  putils->raws.headstx = net.headstx;
  putils->sockidnl = net.sockidnl;
  putils->nbraws = net.nbraws;
#else // RAW
  static uint8_t llchd_tx[4];
  static uint8_t ieeehd_tx[24];
  static uint8_t radiotaphd_tx[13];
  static wfb_net_heads_tx_t headstx = { radiotaphd_tx, sizeof(radiotaphd_tx),
                                            ieeehd_tx, sizeof(ieeehd_tx),
                                            llchd_tx, sizeof(llchd_tx) };
  putils->raws.headstx = &headstx;
  putils->nbraws = 1;
#endif // RAW
      
/*****************************************************************************/  
  putils->log.addrout.sin_family = AF_INET;
  putils->log.addrout.sin_port = htons(PORT_LOG);
  putils->log.addrout.sin_addr.s_addr = inet_addr(IP_LOCAL);
  putils->log.fd = socket(AF_INET, SOCK_DGRAM, 0);
         
/*****************************************************************************/  
  putils->fd[0] = timerfd_create(CLOCK_MONOTONIC, 0);
  putils->readsets[0].fd = putils->fd[0];
  putils->readsets[0].events = POLLIN;
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(putils->fd[0], 0, &period, NULL);

/*****************************************************************************/  
  putils->readtabnb = 1;
  for(uint8_t i=0;i<putils->nbraws;i++) {
#if RAW
#if BOARD
    net.rawdevs[i]->stat.freqfree = false;
#else
    net.rawdevs[i]->stat.freqfree = true;
#endif // BOARD
    uint8_t nextfreqnb = i * (uint8_t) ((net.rawdevs[i]->nbfreqs) / putils->nbraws);
    if (!(wfb_net_setfreq(putils->sockidnl, net.rawdevs[i]->ifindex, net.rawdevs[i]->freqs[nextfreqnb]))) continue;
    net.rawdevs[i]->stat.freqnb = nextfreqnb;
    putils->fd[putils->readtabnb]  = net.rawdevs[i]->sockfd;
    putils->rawdevs[(putils->readtabnb) - 1] = net.rawdevs[i];
#else // RAW
    if (-1 == (putils->fd[putils->readtabnb] = socket(AF_INET, SOCK_DGRAM, 0))) continue;
    if (-1 == setsockopt(putils->fd[putils->readtabnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) continue;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT_NORAW);
    addr.sin_addr.s_addr =inet_addr(IP_LOCAL_RAW);
    if (-1 == bind( putils->fd[putils->readtabnb], (const struct sockaddr *)&addr, sizeof(addr))) continue;
    putils->norawout.sin_family = AF_INET;
    putils->norawout.sin_port = htons(PORT_NORAW);
    putils->norawout.sin_addr.s_addr = inet_addr(IP_REMOTE_RAW);
#endif // RAW
    putils->readsets[putils->readtabnb].fd = putils->fd[putils->readtabnb];
    putils->readsets[putils->readtabnb].events = POLLIN;
    (putils->readtabnb) += 1;
  }
  putils->nbraws = putils->readtabnb - 1;

  build_tun(&putils->fd[putils->readtabnb]); // One bidirectional link
  putils->readsets[putils->readtabnb].fd = putils->fd[putils->readtabnb];
  putils->readsets[putils->readtabnb].events = POLLIN;
  (putils->readtabnb) += 1;

/*****************************************************************************/  
  (putils->readtabnb) += 1;

  if (-1 == (putils->fd[putils->readtabnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
#if BOARD
  if (-1 == setsockopt( putils->fd[putils->readtabnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT_VID);
  addr.sin_addr.s_addr =inet_addr(IP_LOCAL);
  if (-1 == bind( putils->fd[putils->readtabnb], (const struct sockaddr *)&addr, sizeof(addr))) exit(-1);
  putils->readsets[putils->readtabnb].fd = putils->fd[putils->readtabnb];
  putils->readsets[putils->readtabnb].events = POLLIN;
  (putils->readtabnb) += 1;
#else
  putils->vidout.sin_family = AF_INET;
  putils->vidout.sin_port = htons(PORT_VID);
  putils->vidout.sin_addr.s_addr = inet_addr(IP_LOCAL);
#endif // BOARD

/*****************************************************************************/  
  for (uint8_t i=0; i < putils->nbraws; i++) {
    putils->msgin.eltin[i].curseq = 0;
    putils->msgin.eltin[i].nxtseq = 0;
    putils->msgin.eltin[i].nxtfec = 0;
    putils->msgin.eltin[i].fails = false;
    for (uint8_t k=0; k < MAXNBMTUIN; k++) {
      struct iovec *piov = &putils->msgin.eltin[i].iovraw[k];
      piov->iov_base = &putils->msgin.eltin[i].buf_raw[k][0];
    }
    for (uint8_t k=0; k < FEC_N; k++) {
      putils->msgin.eltin[i].iovfec[k].iov_len = 0;
    }
  }

  for (uint8_t j=0;j<putils->nbraws; j++) {
    putils->msgout.eltout[j].seq = 0;
    putils->msgout.eltout[j].num = 0;
  }
  putils->msgout.currvid = 0;

  for (uint8_t i=0;i<WFB_NB;i++) {
    for (uint8_t j=0;j<putils->nbraws; j++) {
      for (uint8_t k=0; k<FEC_N ; k++) {
        struct iovec *piov = &putils->msgout.iov[i][j][k];
	piov->iov_len = 0;
	if (i==WFB_PRO) piov->iov_base = &putils->msgout.buf_pro[j][0];
	if (i==WFB_TUN) piov->iov_base = &putils->msgout.buf_tun[0];
	if (i==WFB_VID)  piov->iov_base = &putils->msgout.buf_vid[k][0];
      }
    }
  }
}
