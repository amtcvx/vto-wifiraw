#if BOARD
#include <termios.h>
#endif // BOARD

#include "wfb_utils.h"

#define FREESECS 10
#define SYNCSECS  2

#if RAW
/*****************************************************************************/
void printlog(wfb_utils_init_t *u, wfb_net_init_t *n) {

  uint8_t template[]="devraw(%d) freq(%ld) mainraw(%d) backraw(%d) fails(%d) sent(%d)\n";
  wfb_utils_log_t *plog = &u->log;
  for (uint8_t i=0; i < n->nbraws; i++) {
    wfb_net_status_t *pst = &(n->rawdevs[i]->stat);
    plog->len += sprintf((char *)plog->txt + plog->len, (char *)template,
                          i, n->rawdevs[i]->freqs[pst->freqnb], n->rawchan.mainraw, n->rawchan.backraw,
                          pst->cumfails, pst->sent);
    pst->cumfails = 0;
  }
  sendto(plog->fd, plog->txt, plog->len, 0,  (const struct sockaddr *)&plog->addr, sizeof(struct sockaddr));
  plog->len = 0;
}
#endif // RAW
     
/*****************************************************************************/
#if BOARD
#else
void wfb_utils_sendfec(fec_t *fec_p, uint8_t hdseq,  uint8_t hdfec, void *base,  wfb_utils_fec_t *pu) {
  bool clearflag = false;

  if (hdfec < FEC_K) {

    if (pu->msgincurseq < 0) pu->msgincurseq = hdseq;

    int16_t nextseqtmp = pu->msginnxtseq; if (nextseqtmp < 255) nextseqtmp++ ; else nextseqtmp = 0;

    if ((pu->inblockstofec >= 0) && (pu->failfec < 0) && (
        ((pu->msginnxtseq != hdseq) && (pu->msginnxtfec != hdfec)) ||
        ((pu->msginnxtseq == hdseq) && (pu->msginnxtfec != hdfec)) ||
        ((nextseqtmp  == hdseq) && (pu->msginnxtfec == (FEC_K - 1)))))  {

      pu->failfec = pu->msginnxtfec;
      if (pu->failfec == 0) pu->bypassflag = false;
    }

    if (hdfec < (FEC_K-1)) pu->msginnxtfec = hdfec+1;
    else { pu->msginnxtfec = 0; if (hdseq < 255) pu->msginnxtseq = hdseq+1; else pu->msginnxtseq = 0; }
  }

  uint8_t imax=0, imin=0;
  if (pu->msgincurseq == hdseq) {

    if (hdfec < FEC_K) {
      
      if ((pu->failfec < 0) || ((pu->failfec > 0) && (hdfec < pu->failfec))) { imin = hdfec; imax = (imin+1); }
      pu->inblocks[hdfec] = base; pu->index[hdfec] = hdfec; pu->alldata |= (1 << hdfec); 
      pu->inblocksnb++;

    } else {

      for (uint8_t k=0;k<FEC_K;k++) if (!(pu->inblocks[k])) {
        pu->inblocks[k] = base; pu->index[k] = hdfec; pu->alldata |= (1 << k);
        pu->outblocks[pu->recovcpt]=&pu->outblocksbuf[pu->recovcpt][0]; pu->outblockrecov[pu->recovcpt] = k; pu->recovcpt++;
        break;
      }
    }

  } else {

    pu->msgincurseq = hdseq;
    pu->inblocks[FEC_K] = base;
    clearflag=true;

    imin = FEC_K; imax = (FEC_K+1);

    if (pu->inblockstofec >= 0) {

      if ((pu->failfec == 0) && (!(pu->bypassflag))) { imin = 0; imax = 0; }

      if ((pu->failfec > 0) || ((pu->failfec == 0) && (pu->bypassflag))) {

        imin = pu->failfec;

        if (!(pu->recovcpt > 0) && ((pu->recovcpt + pu->inblocksnb) == FEC_K) && (pu->alldata == 255)) {
          for (uint8_t k=0;k<pu->recovcpt;k++) pu->inblocks[ pu->outblockrecov[k] ] = 0;
        } else {
          imin = pu->outblockrecov[0];

          fec_decode(fec_p,
                     (const unsigned char **)pu->inblocks,
                     (unsigned char * const*)pu->outblocks,
                     (unsigned int *)pu->index,
                     ONLINE_MTU);

          for (uint8_t k=0;k<pu->recovcpt;k++) pu->inblocks[ pu->outblockrecov[k] ] = pu->outblocks[k];
        }
      }
    } 
  }

  for (uint8_t i=imin;i<imax;i++) {
    uint8_t *ptr=pu->inblocks[i];
    if (ptr) {
      ssize_t vidlen = ((wfb_utils_fechd_t *)ptr)->feclen - sizeof(wfb_utils_fechd_t);
      if (vidlen <= PAY_MTU) {
        ptr += sizeof(wfb_utils_fechd_t);
        vidlen = sendto(pu->fdvid, ptr, vidlen, MSG_DONTWAIT, (struct sockaddr *)&pu->vidoutaddr, sizeof(pu->vidoutaddr));
      }
    }
  }

  if (clearflag) {

    if ((pu->failfec == 0)&&(!(pu->bypassflag))) pu->bypassflag = true;
    else pu->failfec = -1;

    clearflag=false;
    pu->msginnxtseq = hdseq;
    pu->inblockstofec = hdfec;

    memset(pu->inblocks, 0, (FEC_K * sizeof(uint8_t *)));

    pu->inblocksnb=0; pu->recovcpt=0;
    if (hdfec < FEC_K) { pu->inblocks[hdfec] = pu->inblocks[FEC_K];
      pu->index[hdfec] = hdfec; pu->alldata |= (1 << hdfec);
      pu->inblocksnb=1;
    }
  }
}
#endif // BOARD

/*****************************************************************************/
#if RAW
void setmainbackup(wfb_net_init_t *p, ssize_t lentab[WFB_NB][MAXRAWDEV] ,int16_t probuf[MAXRAWDEV]) {
#if BOARD
  for (uint8_t rawcpt=0; rawcpt < p->nbraws; rawcpt++) {
    wfb_net_status_t *pst = &(p->rawdevs[rawcpt]->stat);

    if (pst->fails != 0) { pst->freqfree = false; pst->timecpt = 0; pst->cumfails += pst->fails; pst->fails = 0; }
    else if (pst->timecpt < FREESECS) (pst->timecpt)++; else { pst->freqfree = true; pst->timecpt = 0; }
  }

  if (p->rawchan.mainraw < 0) {
    for (uint8_t rawcpt=0; rawcpt < p->nbraws; rawcpt++) if (p->rawdevs[rawcpt]->stat.freqfree) p->rawchan.mainraw = rawcpt;
  } else {
    if ((!(p->rawdevs[p->rawchan.mainraw]->stat.freqfree)) && (p->rawchan.backraw >=0 ) && (p->rawdevs[p->rawchan.backraw]->stat.freqfree))
      { p->rawchan.mainraw = p->rawchan.backraw; p->rawchan.backraw = -1; }
  }

  if (p->rawchan.backraw < 0) {
    for (uint8_t rawcpt=0; rawcpt < p->nbraws; rawcpt++) if ((p->rawdevs[rawcpt]->stat.freqfree) && (p->rawchan.mainraw != rawcpt)) p->rawchan.backraw = rawcpt;
  } else if (!(p->rawdevs[p->rawchan.backraw]->stat.freqfree)) p->rawchan.backraw = -1;

  for (uint8_t rawcpt=0; rawcpt < p->nbraws; rawcpt++) {
    if ((rawcpt != p->rawchan.mainraw) && (rawcpt != p->rawchan.backraw) && (!(p->rawdevs[rawcpt]->stat.freqfree)) && (p->rawdevs[rawcpt]->stat.timecpt ==0)) {

      if (p->rawdevs[rawcpt]->stat.freqnb < (p->rawdevs[rawcpt]->nbfreqs - 1)) (p->rawdevs[rawcpt]->stat.freqnb)++; else p->rawdevs[rawcpt]->stat.freqnb = 0;
      for (uint8_t i=0; i < p->nbraws; i++) { 
        if ((i != rawcpt) &&  (p->rawdevs[i]->freqs[ p->rawdevs[i]->stat.freqnb] == p->rawdevs[rawcpt]->freqs[ p->rawdevs[rawcpt]->stat.freqnb])) {
          if (p->rawdevs[rawcpt]->stat.freqnb < (p->rawdevs[rawcpt]->nbfreqs - 1)) (p->rawdevs[rawcpt]->stat.freqnb)++; else p->rawdevs[rawcpt]->stat.freqnb = 0;
	}
      }
      wfb_net_setfreq(&p->sockidnl, p->rawdevs[rawcpt]->ifindex, p->rawdevs[rawcpt]->freqs[p->rawdevs[rawcpt]->stat.freqnb]);
    }
  }

  if (p->rawchan.mainraw >= 0) {
    lentab[WFB_PRO][p->rawchan.mainraw] = 1;
    probuf[p->rawchan.mainraw] = -1;

    if (!(p->rawdevs[p->rawchan.mainraw]->stat.syncelapse)) lentab[WFB_PRO][p->rawchan.mainraw] = 1; else lentab[WFB_PRO][p->rawchan.mainraw] = 0;
    p->rawdevs[p->rawchan.mainraw]->stat.syncelapse = false;


    if (p->rawchan.backraw >= 0) {
      lentab[WFB_PRO][p->rawchan.mainraw] = 1;
      probuf[p->rawchan.mainraw] = p->rawdevs[p->rawchan.backraw]->stat.freqnb;
      probuf[p->rawchan.backraw] = -(p->rawdevs[p->rawchan.mainraw]->stat.freqnb);
    }
  }
#else
  for (uint8_t rawcpt=0; rawcpt < p->nbraws; rawcpt++) {
    wfb_net_status_t *pst = &(p->rawdevs[rawcpt]->stat);

    if (pst->fails != 0) { pst->cumfails += pst->fails; pst->fails = 0; }

    if (probuf[rawcpt] == 0) {

      if (pst->timecpt < SYNCSECS) (pst->timecpt)++;
      else {
        pst->timecpt = 0;
	if ((rawcpt != p->rawchan.mainraw) && (rawcpt != p->rawchan.backraw)) {

          if (p->rawdevs[rawcpt]->stat.freqnb < (p->rawdevs[rawcpt]->nbfreqs - 1)) (p->rawdevs[rawcpt]->stat.freqnb)++; else p->rawdevs[rawcpt]->stat.freqnb = 0;
	  for (uint8_t i=0; i < p->nbraws; i++) {
            if ((i != rawcpt) &&  (p->rawdevs[i]->freqs[ p->rawdevs[i]->stat.freqnb] == p->rawdevs[rawcpt]->freqs[ p->rawdevs[rawcpt]->stat.freqnb])) {
              if (p->rawdevs[rawcpt]->stat.freqnb < (p->rawdevs[rawcpt]->nbfreqs - 1)) (p->rawdevs[rawcpt]->stat.freqnb)++; else p->rawdevs[rawcpt]->stat.freqnb = 0;
            }
	  }
          wfb_net_setfreq(&p->sockidnl, p->rawdevs[rawcpt]->ifindex, p->rawdevs[rawcpt]->freqs[p->rawdevs[rawcpt]->stat.freqnb]);
	}
      }
    }
  }

#endif // BOARD
}
#endif  // RAW

/*****************************************************************************/
#if RAW
#if BOARD
#else
void wfb_utils_syncground(wfb_utils_init_t *u, wfb_net_init_t *n, uint8_t rawcpt) {

  int16_t curchan =  n->rawdevs[rawcpt]->stat.syncchan;

  if (curchan == -1) { n->rawchan.mainraw = rawcpt; n->rawchan.backraw = -1; }
  else {
    int16_t newchan = 0;
    uint8_t newraw = 0;

    if (n->nbraws == 1) {
      n->rawchan.mainraw = rawcpt; n->rawchan.backraw = -1;
      if (curchan < 0) newchan = (-curchan);
      newraw = n->rawchan.mainraw;
    } else {
      for (newraw=0; newraw < n->nbraws; newraw++) if (newraw != rawcpt) break;
      if (newraw != rawcpt) {
        if (curchan > 0) { n->rawchan.mainraw = rawcpt; n->rawchan.backraw = newraw; newchan = curchan; }
        if (curchan < 0) { n->rawchan.mainraw = newraw; n->rawchan.backraw = rawcpt; newchan = (-curchan); }
      }
    }

    if (newchan > 0) {
      uint8_t cpt = 0; for (cpt=0; cpt < n->rawdevs[newraw]->nbfreqs; cpt++) if (n->rawdevs[newraw]->freqs[cpt] == newchan) break;
      if (n->rawdevs[newraw]->freqs[cpt] == newchan) {
        if (n->rawdevs[newraw]->stat.freqnb != cpt) {
          n->rawdevs[newraw]->stat.freqnb = cpt;
          wfb_net_setfreq(&n->sockidnl, n->rawdevs[newraw]->ifindex, n->rawdevs[newraw]->freqs[n->rawdevs[newraw]->stat.freqnb]);
	}
      }
    }
  }
}
#endif //  BOARD
#endif //  RAW


/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *u, wfb_net_init_t *n,ssize_t lentab[WFB_NB][MAXRAWDEV] ,int16_t probuf[MAXRAWDEV]) {
#if RAW
  setmainbackup(n,lentab,probuf);
  printlog(u,n);
#endif  // RAW
}

#if RAW
/*****************************************************************************/
void wfb_utils_addraw(wfb_utils_init_t *pu, wfb_net_init_t *pn) {

  for (uint8_t i=0;i < pn->nbraws; i++) {
    pu->readtab[pu->readnb] = WFB_NB; pu->socktab[WFB_NB] = pu->readnb;
    pu->fd[pu->readnb] = pn->rawdevs[i]->sockfd;
    pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;
  }
}
#endif // RAW

/*****************************************************************************/
uint8_t build_tun(void) {
  uint8_t fd;
  if (0 > (fd = open("/dev/net/tun",O_RDWR))) exit(-1);

  struct ifreq ifr; memset(&ifr, 0, sizeof(struct ifreq));
  strcpy(ifr.ifr_name, TUN_NAME);
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (ioctl(fd, TUNSETIFF, &ifr ) < 0 ) exit(-1);

  static uint16_t fd_tun_udp;
  if (-1 == (fd_tun_udp = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP))) exit(-1);
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;

  addr.sin_addr.s_addr = inet_addr(TUN_IP_SRC);
  memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));
  if (ioctl( fd_tun_udp, SIOCSIFADDR, &ifr ) < 0 ) exit(-1);

  addr.sin_addr.s_addr = inet_addr(IPBROAD);
  memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));
  if (ioctl( fd_tun_udp, SIOCSIFNETMASK, &ifr ) < 0 ) exit(-1);

  struct sockaddr_in dstaddr;
  dstaddr.sin_family = AF_INET;
  dstaddr.sin_addr.s_addr = inet_addr(TUN_IP_DST);
  memcpy(&ifr.ifr_addr, &dstaddr, sizeof(struct sockaddr));
  if (ioctl( fd_tun_udp, SIOCSIFDSTADDR, &ifr ) < 0 ) exit(-1);

  ifr.ifr_mtu = TUN_MTU;
  if (ioctl( fd_tun_udp, SIOCSIFMTU, &ifr ) < 0 ) exit(-1);
  ifr.ifr_flags = IFF_UP ;
  if (ioctl( fd_tun_udp, SIOCSIFFLAGS, &ifr ) < 0 ) exit(-1);

  return(fd);
}

/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *pu) {

  if (-1 == (pu->log.fd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  pu->log.addr.sin_family = AF_INET;
  pu->log.addr.sin_port = htons(PORT_LOG);
  pu->log.addr.sin_addr.s_addr = inet_addr(IP_LOCAL);

  pu->readtab[pu->readnb] = WFB_PRO; pu->socktab[WFB_PRO] = pu->readnb;
  if (-1 == (pu->fd[pu->readnb] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(pu->fd[pu->readnb], 0, &period, NULL);
  pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;

  pu->readtab[pu->readnb] = WFB_TUN; pu->socktab[WFB_TUN] = pu->readnb;
  if (-1 == (pu->fd[pu->readnb] = build_tun())) exit(-1);
  pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;

  pu->readtab[pu->readnb] = WFB_VID; pu->socktab[WFB_VID] = pu->readnb;
  if (-1 == (pu->fd[pu->readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
#if BOARD
  if (-1 == setsockopt(pu->fd[pu->readnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in  vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(PORT_VID);
  vidinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL);
  if (-1 == bind( pu->fd[pu->readnb], (const struct sockaddr *)&vidinaddr, sizeof( vidinaddr))) exit(-1);
  pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;
#else
  pu->fec.fdvid = pu->fd[pu->readnb];
  pu->fec.vidoutaddr.sin_family = AF_INET;
  pu->fec.vidoutaddr.sin_port = htons(PORT_VID);
  pu->fec.vidoutaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);
#endif // BOARD

  fec_new(FEC_K, FEC_N, &pu->fec_p);
#if BOARD
#else
  pu->fec.inblockstofec = -1;
  pu->fec.failfec = -1;
  pu->fec.msginnxtseq = -1;
  pu->fec.msgincurseq = -1;
  pu->fec.bypassflag = false;
#endif // BOARD
}
