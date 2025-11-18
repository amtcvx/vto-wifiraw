#if BOARD
#include <termios.h>
#endif // BOARD

#include "wfb_utils.h"

#define FREESECS 10
#define SYNCSECS  2

#if RAW
/*****************************************************************************/
void printlog(wfb_utils_init_t *u, wfb_net_init_t *n) {

  uint8_t template[]="devraw(%d) freq(%ld) mainraw(%d) backraw(%d) fails(%d) sent(%d)\n\n";
  wfb_utils_log_t *plog = &u->log;
  for (uint8_t i=0; i < n->nbraws; i++) {
    wfb_net_status_t *pst = &(n->rawdevs[i]->stat);
    plog->len += sprintf((char *)plog->txt + plog->len, (char *)template,
                          i, n->rawdevs[i]->freqs[pst->freqnb], n->rawchan.mainraw, n->rawchan.backraw,
                          pst->fails, pst->sent);
    pst->fails = 0; pst->sent = 0; 
  }
  sendto(plog->fd, plog->txt, plog->len, 0,  (const struct sockaddr *)&plog->addr, sizeof(struct sockaddr));
  plog->len = 0;
}
#endif // RAW
     
/*****************************************************************************/
#if BOARD
#else
void wfb_utils_sendfec(wfb_utils_fec_t *uf, fec_t *fec_p, uint8_t hdseq,  uint8_t hdfec, void *base) {
  bool clearflag = false;

  if (hdfec < FEC_K) {

    if (uf->msgincurseq < 0) uf->msgincurseq = hdseq;

    int16_t nextseqtmp = uf->msginnxtseq; if (nextseqtmp < 255) nextseqtmp++ ; else nextseqtmp = 0;

    if ((uf->inblockstofec >= 0) && (uf->failfec < 0) && (
        ((uf->msginnxtseq != hdseq) && (uf->msginnxtfec != hdfec)) ||
        ((uf->msginnxtseq == hdseq) && (uf->msginnxtfec != hdfec)) ||
        ((nextseqtmp  == hdseq) && (uf->msginnxtfec == (FEC_K - 1)))))  {

      uf->failfec = uf->msginnxtfec;
      if (uf->failfec == 0) uf->bypassflag = false;
    }

    if (hdfec < (FEC_K-1)) uf->msginnxtfec = hdfec+1;
    else { uf->msginnxtfec = 0; if (hdseq < 255) uf->msginnxtseq = hdseq+1; else uf->msginnxtseq = 0; }
  }

  uint8_t imax=0, imin=0;
  if (uf->msgincurseq == hdseq) {

    if (hdfec < FEC_K) {
      
      if ((uf->failfec < 0) || ((uf->failfec > 0) && (hdfec < uf->failfec))) { imin = hdfec; imax = (imin+1); }
      uf->inblocks[hdfec] = base; uf->index[hdfec] = hdfec; uf->alldata |= (1 << hdfec); 
      uf->inblocksnb++;

    } else {

      for (uint8_t k=0;k<FEC_K;k++) if (!(uf->inblocks[k])) {
        uf->inblocks[k] = base; uf->index[k] = hdfec; uf->alldata |= (1 << k);
        uf->outblocks[uf->recovcpt]=&uf->outblocksbuf[uf->recovcpt][0]; uf->outblockrecov[uf->recovcpt] = k; uf->recovcpt++;
        break;
      }
    }

  } else {

    uf->msgincurseq = hdseq;
    uf->inblocks[FEC_K] = base;
    clearflag=true;

    imin = FEC_K; imax = (FEC_K+1);

    if (uf->inblockstofec >= 0) {

      if ((uf->failfec == 0) && (!(uf->bypassflag))) { imin = 0; imax = 0; }

      if ((uf->failfec > 0) || ((uf->failfec == 0) && (uf->bypassflag))) {

        imin = uf->failfec;

        if (!(uf->recovcpt > 0) && ((uf->recovcpt + uf->inblocksnb) == FEC_K) && (uf->alldata == 255)) {
          for (uint8_t k=0;k<uf->recovcpt;k++) uf->inblocks[ uf->outblockrecov[k] ] = 0;
        } else {
          imin = uf->outblockrecov[0];

          fec_decode(fec_p,
                     (const unsigned char **)uf->inblocks,
                     (unsigned char * const*)uf->outblocks,
                     (unsigned int *)uf->index,
                     ONLINE_MTU);

          for (uint8_t k=0;k<uf->recovcpt;k++) uf->inblocks[ uf->outblockrecov[k] ] = uf->outblocks[k];
        }
      }
    } 
  }

  for (uint8_t i=imin;i<imax;i++) {
    uint8_t *ptr=uf->inblocks[i];
    if (ptr) {
      ssize_t vidlen = ((wfb_utils_fechd_t *)ptr)->feclen - sizeof(wfb_utils_fechd_t);
      if (vidlen <= PAY_MTU) {
        ptr += sizeof(wfb_utils_fechd_t);
        vidlen = sendto(uf->fdvid, ptr, vidlen, MSG_DONTWAIT, (struct sockaddr *)&uf->vidoutaddr, sizeof(uf->vidoutaddr));
      }
    }
  }

  if (clearflag) {

    if ((uf->failfec == 0)&&(!(uf->bypassflag))) uf->bypassflag = true;
    else uf->failfec = -1;

    clearflag=false;
    uf->msginnxtseq = hdseq;
    uf->inblockstofec = hdfec;

    memset(uf->inblocks, 0, (FEC_K * sizeof(uint8_t *)));

    uf->inblocksnb=0; uf->recovcpt=0;
    if (hdfec < FEC_K) { uf->inblocks[hdfec] = uf->inblocks[FEC_K];
      uf->index[hdfec] = hdfec; uf->alldata |= (1 << hdfec);
      uf->inblocksnb=1;
    }
  }
}
#endif // BOARD

/*****************************************************************************/
#if RAW
void setmainbackup(wfb_utils_init_t *u, wfb_net_init_t *n, ssize_t lentab[WFB_NB][MAXRAWDEV] ,int16_t probuf[MAXRAWDEV]) {
#if BOARD
  for (uint8_t rawcpt=0; rawcpt < n->nbraws; rawcpt++) {
    wfb_net_status_t *pst = &(n->rawdevs[rawcpt]->stat);

    u->log.len += sprintf((char *)u->log.txt + u->log.len,  "IN raw[%d] index[%d] setmainbackup [%d][%d][%d]  [%d]\n",
      rawcpt, n->rawdevs[rawcpt]->ifindex, pst->synccum, pst->synccpt, pst->syncfree, true);

    if (pst->synccum != 0) { pst->syncfree = false; pst->synccpt = 0; pst->fails = pst->synccum; pst->synccum = 0; }
    else if (pst->synccpt < FREESECS) (pst->synccpt)++; 
      else { pst->syncfree = true; pst->synccpt = 0; }

    u->log.len += sprintf((char *)u->log.txt + u->log.len,  "OUT raw[%d] index[%d] setmainbackup [%d][%d][%d]  [%d]\n",
      rawcpt, n->rawdevs[rawcpt]->ifindex, pst->synccum, pst->synccpt, pst->syncfree, true);
  }

  if (n->rawchan.mainraw < 0) {
    for (uint8_t rawcpt=0; rawcpt < n->nbraws; rawcpt++) 
      if (n->rawdevs[rawcpt]->stat.syncfree) n->rawchan.mainraw = rawcpt;
  } else {
    if ((!(n->rawdevs[n->rawchan.mainraw]->stat.syncfree)) && (n->rawchan.backraw >=0 ) && (n->rawdevs[n->rawchan.backraw]->stat.syncfree))
      { n->rawchan.mainraw = n->rawchan.backraw; n->rawchan.backraw = -1; }
  }

  if (n->rawchan.backraw < 0) {
    for (uint8_t rawcpt=0; rawcpt < n->nbraws; rawcpt++) 
      if ((n->rawdevs[rawcpt]->stat.syncfree) && (n->rawchan.mainraw != rawcpt)) n->rawchan.backraw = rawcpt;
  } else if (!(n->rawdevs[n->rawchan.backraw]->stat.syncfree)) n->rawchan.backraw = -1;

  for (uint8_t rawcpt=0; rawcpt < n->nbraws; rawcpt++) {
    if ((rawcpt != n->rawchan.mainraw) && (rawcpt != n->rawchan.backraw) &&
      (!(n->rawdevs[rawcpt]->stat.syncfree) && (n->rawdevs[rawcpt]->stat.synccpt ==0))) {

      if (n->rawdevs[rawcpt]->stat.freqnb < (n->rawdevs[rawcpt]->nbfreqs - 1)) (n->rawdevs[rawcpt]->stat.freqnb)++; 
      else n->rawdevs[rawcpt]->stat.freqnb = 0;

      for (uint8_t i=0; i < n->nbraws; i++) { 
        if ((i != rawcpt) &&  (n->rawdevs[i]->freqs[ n->rawdevs[i]->stat.freqnb] == n->rawdevs[rawcpt]->freqs[ n->rawdevs[rawcpt]->stat.freqnb])) {
          if (n->rawdevs[rawcpt]->stat.freqnb < (n->rawdevs[rawcpt]->nbfreqs - 1)) (n->rawdevs[rawcpt]->stat.freqnb)++; 
	  else n->rawdevs[rawcpt]->stat.freqnb = 0;
        }
      }
      wfb_net_setfreq(&n->sockidnl, n->rawdevs[rawcpt]->ifindex, n->rawdevs[rawcpt]->freqs[n->rawdevs[rawcpt]->stat.freqnb]);

      u->log.len += sprintf((char *)u->log.txt + u->log.len,  "B raw[%d] index[%d] setfreq [%d][%d]\n",
        rawcpt, n->rawdevs[rawcpt]->ifindex, n->rawdevs[rawcpt]->stat.freqnb, n->rawdevs[rawcpt]->freqs[n->rawdevs[rawcpt]->stat.freqnb]);
    }
  }

  if (n->rawchan.mainraw >= 0) {
    lentab[WFB_PRO][n->rawchan.mainraw] = 1;
    probuf[n->rawchan.mainraw] = -1;

    if (!(n->rawdevs[n->rawchan.mainraw]->stat.syncelapse)) lentab[WFB_PRO][n->rawchan.mainraw] = 1; else lentab[WFB_PRO][n->rawchan.mainraw] = 0;
    n->rawdevs[n->rawchan.mainraw]->stat.syncelapse = false;

    if (n->rawchan.backraw >= 0) {
      lentab[WFB_PRO][n->rawchan.backraw] = 1;
      probuf[n->rawchan.mainraw] = n->rawdevs[n->rawchan.backraw]->stat.freqnb;
      probuf[n->rawchan.backraw] = -(n->rawdevs[n->rawchan.mainraw]->stat.freqnb);
    }
  }
#else
  for (uint8_t rawcpt=0; rawcpt < n->nbraws; rawcpt++) {
    wfb_net_status_t *pst = &(n->rawdevs[rawcpt]->stat);

    if (pst->synccum != 0) { pst->fails = pst->synccum; pst->synccum = 0; }

    if (probuf[rawcpt] == 0) {

      if (pst->synccpt < SYNCSECS) (pst->synccpt)++;
      else {
        pst->synccpt = 0;
	if ((rawcpt != n->rawchan.mainraw) && (rawcpt != n->rawchan.backraw)) {

          if (n->rawdevs[rawcpt]->stat.freqnb < (n->rawdevs[rawcpt]->nbfreqs - 1)) (n->rawdevs[rawcpt]->stat.freqnb)++; 
	  else n->rawdevs[rawcpt]->stat.freqnb = 0;

	  for (uint8_t i=0; i < n->nbraws; i++) {
            if ((i != rawcpt) && (n->rawdevs[i]->freqs[ n->rawdevs[i]->stat.freqnb] == n->rawdevs[rawcpt]->freqs[ n->rawdevs[rawcpt]->stat.freqnb])) {
              if (n->rawdevs[rawcpt]->stat.freqnb < (n->rawdevs[rawcpt]->nbfreqs - 1)) (n->rawdevs[rawcpt]->stat.freqnb)++; 
	      else n->rawdevs[rawcpt]->stat.freqnb = 0;
            }
	  }
          wfb_net_setfreq(&n->sockidnl, n->rawdevs[rawcpt]->ifindex, n->rawdevs[rawcpt]->freqs[n->rawdevs[rawcpt]->stat.freqnb]);

	  u->log.len += sprintf((char *)u->log.txt + u->log.len,  "B raw[%d] index[%d] setfreq [%d][%d]\n",
	    rawcpt, n->rawdevs[rawcpt]->ifindex, n->rawdevs[rawcpt]->stat.freqnb, n->rawdevs[rawcpt]->freqs[n->rawdevs[rawcpt]->stat.freqnb]);
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
#else // BOARD
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
          wfb_net_setfreq(&n->sockidnl, n->rawdevs[newraw]->ifindex, n->rawdevs[newraw]->freqs[cpt]);

	  u->log.len += sprintf((char *)u->log.txt + u->log.len,  "C raw[%d] index[%d] setfreq [%d][%d]\n",
	    newraw, n->rawdevs[newraw]->ifindex, n->rawdevs[newraw]->stat.freqnb, n->rawdevs[newraw]->freqs[n->rawdevs[newraw]->stat.freqnb]);
	}
      }
    }
  }
}
#endif //  BOARD
#endif  // RAW


/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *u, wfb_net_init_t *n,ssize_t lentab[WFB_NB][MAXRAWDEV] ,int16_t probuf[MAXRAWDEV]) {
#if RAW
  setmainbackup(u,n,lentab,probuf);
  printlog(u,n);
#endif  // RAW
}

#if RAW
/*****************************************************************************/
void wfb_utils_addraw(wfb_utils_init_t *u, wfb_net_init_t *n) {

  for (uint8_t rawcpt=0; rawcpt < n->nbraws; rawcpt++) {

    u->readtab[u->readnb] = WFB_NB; u->socktab[WFB_NB] = u->readnb;
    u->fd[u->readnb] = n->rawdevs[rawcpt]->sockfd;
    u->readsets[u->readnb].fd = u->fd[u->readnb]; u->readsets[u->readnb].events = POLLIN; u->readnb++;

    memset(&(n->rawdevs[rawcpt]->stat), 0, sizeof(wfb_net_status_t));
    n->rawdevs[rawcpt]->stat.syncfree = true;

    n->rawdevs[rawcpt]->stat.freqnb = (n->nbraws - rawcpt - 1) * (n->rawdevs[rawcpt]->nbfreqs / n->nbraws);
    wfb_net_setfreq(&n->sockidnl, n->rawdevs[rawcpt]->ifindex, n->rawdevs[rawcpt]->freqs[n->rawdevs[rawcpt]->stat.freqnb]);

    u->log.len += sprintf((char *)u->log.txt + u->log.len,  "A raw[%d] index[%d] setfreq [%d][%d]\n",
      rawcpt, n->rawdevs[rawcpt]->ifindex, n->rawdevs[rawcpt]->stat.freqnb, n->rawdevs[rawcpt]->freqs[n->rawdevs[rawcpt]->stat.freqnb]);
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
void wfb_utils_init(wfb_utils_init_t *u) {

  if (-1 == (u->log.fd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  u->log.addr.sin_family = AF_INET;
  u->log.addr.sin_port = htons(PORT_LOG);
  u->log.addr.sin_addr.s_addr = inet_addr(IP_LOCAL);

  u->readtab[u->readnb] = WFB_PRO; u->socktab[WFB_PRO] = u->readnb;
  if (-1 == (u->fd[u->readnb] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(u->fd[u->readnb], 0, &period, NULL);
  u->readsets[u->readnb].fd = u->fd[u->readnb]; u->readsets[u->readnb].events = POLLIN; u->readnb++;

  u->readtab[u->readnb] = WFB_TUN; u->socktab[WFB_TUN] = u->readnb;
  if (-1 == (u->fd[u->readnb] = build_tun())) exit(-1);
  u->readsets[u->readnb].fd = u->fd[u->readnb]; u->readsets[u->readnb].events = POLLIN; u->readnb++;

  u->readtab[u->readnb] = WFB_VID; u->socktab[WFB_VID] = u->readnb;
  if (-1 == (u->fd[u->readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
#if BOARD
  if (-1 == setsockopt(u->fd[u->readnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in  vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(PORT_VID);
  vidinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL);
  if (-1 == bind( u->fd[u->readnb], (const struct sockaddr *)&vidinaddr, sizeof( vidinaddr))) exit(-1);
  u->readsets[u->readnb].fd = u->fd[u->readnb]; u->readsets[u->readnb].events = POLLIN; u->readnb++;
#else
  u->fec.fdvid = u->fd[u->readnb];
  u->fec.vidoutaddr.sin_family = AF_INET;
  u->fec.vidoutaddr.sin_port = htons(PORT_VID);
  u->fec.vidoutaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);
#endif // BOARD

  fec_new(FEC_K, FEC_N, &u->fec_p);
#if BOARD
#else
  u->fec.inblockstofec = -1;
  u->fec.failfec = -1;
  u->fec.msginnxtseq = -1;
  u->fec.msgincurseq = -1;
  u->fec.bypassflag = false;
#endif // BOARD
}
