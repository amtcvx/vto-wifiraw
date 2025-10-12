#if BOARD
#include <termios.h>
#endif // BOARD

#include "wfb_utils.h"

/*****************************************************************************/
#if BOARD
#else
void wfb_utils_sendfec(fec_t *fec_p, uint8_t hdseq,  uint8_t hdfec, void *base,  wfb_utils_fec_t *pu) {
  bool clearflag = false;

  if (hdfec < FEC_K) {
    if (pu->msgincurseq < 0) pu->msgincurseq = hdseq;
    if ((pu->inblockstofec >= 0) && ((pu->msginnxtseq != hdseq) || (pu->msginnxtfec != hdfec))
       && (pu->failfec < 0)) { pu->failfec = pu->msginnxtfec; if (pu->failfec == 0) pu->bypassflag = false; }
    if (hdfec < (FEC_K-1)) pu->msginnxtfec = hdfec+1;
    else { pu->msginnxtfec = 0; if (hdseq < 255) pu->msginnxtseq = hdseq+1; else pu->msginnxtseq = 0; }
  }

  uint8_t imax=0, imin=0;
  if (pu->msgincurseq == hdseq) {
    if (hdfec < FEC_K) {
      if ((pu->failfec < 0) || ((pu->failfec > 0) && (hdfec < pu->failfec))) { imin = hdfec; imax = (imin+1); }
      pu->inblocks[hdfec] = base; pu->index[hdfec] = hdfec; pu->inblocksnb++;
    } else {
      for (uint8_t k=0;k<FEC_K;k++) if (!(pu->inblocks[k])) {
        pu->inblocks[k] = base; pu->index[k] = hdfec;
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
        if ((pu->recovcpt + pu->inblocksnb) != (FEC_K-1))  { for (uint8_t k=0;k<pu->recovcpt;k++) pu->inblocks[ pu->outblockrecov[k] ] = 0; }
        else {
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
      ptr += sizeof(wfb_utils_fechd_t);
      vidlen = sendto(pu->fdvid, ptr, vidlen, MSG_DONTWAIT, (struct sockaddr *)&pu->vidoutaddr, sizeof(pu->vidoutaddr));
    }
  }

  if (clearflag) {
    clearflag=false;
    if ((pu->failfec == 0)&&(!(pu->bypassflag))) pu->bypassflag = true;
    else pu->failfec = -1;
    pu->msginnxtseq = hdseq;
    pu->inblocksnb=0; pu->recovcpt=0;
    memset(pu->inblocks, 0, (FEC_K * sizeof(uint8_t *)));
    pu->inblockstofec = hdfec;
    pu->inblocks[pu->inblockstofec] = pu->inblocks[FEC_K];
  }

}
#endif // BOARD

/*****************************************************************************/
#if RAW

void setmainbackup(wfb_net_init_t *p) {
#if BOARD
  for (uint8_t i=0; i < p->nbraws; i++) {
    wfb_net_status_t *pst = &(p->rawdevs[i]->stat);
    if (pst->fails != 0) {
      pst->fails = 0;
      pst->timecpt = 0;
      pst->freqfree = false;
      if (i != p->rawchan.mainraw) {
        uint8_t nextfreqnb = 1 + p->rawdevs[i]->stat.freqnb;
        if (nextfreqnb > p->rawdevs[i]->nbfreqs) nextfreqnb = 0;
        p->rawdevs[i]->stat.freqnb = nextfreqnb;
        wfb_net_setfreq(&p->sockidnl, p->rawdevs[i]->ifindex, p->rawdevs[i]->freqs[nextfreqnb]);
      }
    } else {
      if (pst->timecpt < 10) pst->timecpt++;
      else {
        pst->timecpt = 0;
        pst->freqfree = true;
      }
    }
  }

  if (p->nbraws == 1) {
    if (p->rawchan.mainraw == -1) {
      for (uint8_t i=0; i < p->nbraws; i++) {
        if (p->rawdevs[i]->stat.freqfree) { p->rawchan.mainraw = i; break; }
      }
    }
  } else {

    if (p->rawchan.mainraw != -1) {
      if (!(p->rawdevs[p->rawchan.mainraw]->stat.freqfree)) {
        p->rawchan.mainraw = -1;
        if (p->rawchan.backraw != -1) {
          if (p->rawdevs[p->rawchan.backraw]->stat.freqfree) {
            p->rawchan.mainraw = p->rawchan.backraw;
            p->rawchan.backraw = -1;
          }
        } else {
          for (uint8_t i=0; i < p->nbraws; i++) {
            if (p->rawdevs[i]->stat.freqfree) { p->rawchan.mainraw = i; break; }
          }
        }
      }
    } else {
      for (uint8_t i=0; i < p->nbraws; i++) {
        if (p->rawdevs[i]->stat.freqfree) { p->rawchan.mainraw = i; break; }
      }
    }

    if (p->rawchan.backraw != -1) {
      if  (!(p->rawdevs[p->rawchan.backraw]->stat.freqfree)) p->rawchan.backraw = -1;
    }
    if ((p->rawchan.mainraw != -1) && (p->rawchan.backraw == -1)) {
      for (uint8_t i=0; i < p->nbraws; i++) {
        if ((i != p->rawchan.mainraw) && (p->rawdevs[i]->stat.freqfree)) { p->rawchan.backraw = i; break; }
      }
    }
  }
/*
  if (p->rawchan.mainraw != -1) {
    p->msgout.iov[WFB_PRO][p->rawchan.mainraw][0].iov_len = sizeof(wfb_utils_pro_t);
    if (p->rawchan.backraw == -1) {
       ((wfb_utils_pro_t *)p->msgout.iov[WFB_PRO][p->rawchan.mainraw][0].iov_base)->chan = -1;
    } else {
      ((wfb_utils_pro_t *)p->msgout.iov[WFB_PRO][p->rawchan.mainraw][0].iov_base)->chan = p->rawdevs[p->rawchan.backraw]->stat.freqnb;
      ((wfb_utils_pro_t *)p->msgout.iov[WFB_PRO][p->rawchan.backraw][0].iov_base)->chan = 100 + p->rawdevs[p->rawchan.mainraw]->stat.freqnb;
      p->msgout.iov[WFB_PRO][p->rawchan.backraw][0].iov_len = sizeof(wfb_utils_pro_t);
    }
  }
*/
#else
  for (uint8_t i=0; i < p->nbraws; i++) {
    wfb_net_status_t *pst = &(p->rawdevs[i]->stat);
    if (pst->incoming > 0) {
      pst->incoming = 0;
      pst->timecpt = 0;
      pst->freqfree = false;

    } else {
      if (pst->timecpt < 3) pst->timecpt++;
      else {
        pst->timecpt = 0;
        pst->freqfree = true;

        if (i == p->rawchan.mainraw) p->rawchan.mainraw = -1;
        if (i == p->rawchan.backraw) p->rawchan.backraw = -1;

        uint8_t nextfreqnb = 1 + pst->freqnb;
        if (nextfreqnb > p->rawdevs[i]->nbfreqs) nextfreqnb = 0;
        pst->freqnb = nextfreqnb;
        wfb_net_setfreq(&p->sockidnl, p->rawdevs[i]->ifindex, p->rawdevs[i]->freqs[nextfreqnb]);
      }
    }
  }
  for (uint8_t i=0; i < p->nbraws; i++) {
    if (!(p->rawdevs[i]->stat.freqfree)) {
      if (p->rawdevs[i]->stat.chan == -1) {
        p->rawchan.mainraw = i;
        p->rawchan.backraw = -1;
        break; // for i
      } else if (p->rawdevs[i]->stat.chan < 100) {
        p->rawchan.mainraw = i;
        for (uint8_t j=0; j < p->nbraws; j++) {
          if (j != p->rawchan.mainraw) {
            p->rawchan.backraw = j;
            if (p->rawdevs[j]->stat.freqnb !=  p->rawdevs[i]->stat.chan) {
              p->rawdevs[j]->stat.freqnb = p->rawdevs[i]->stat.chan;
              wfb_net_setfreq(&p->sockidnl, p->rawdevs[j]->ifindex, p->rawdevs[j]->freqs[ p->rawdevs[j]->stat.freqnb ]);
              break; // for j
            }
          }
        }
        break; // for i
      } else if (p->rawdevs[i]->stat.chan >= 100) {
        p->rawchan.backraw = i;
        for (uint8_t j=0; j < p->nbraws; j++) {
          if (j != p->rawchan.backraw)  {
            p->rawchan.mainraw = j;
            if (p->rawdevs[j]->stat.freqnb !=  (p->rawdevs[i]->stat.chan - 100)) {
              p->rawdevs[j]->stat.freqnb = p->rawdevs[i]->stat.chan - 100;
              wfb_net_setfreq(&p->sockidnl, p->rawdevs[j]->ifindex, p->rawdevs[j]->freqs[ p->rawdevs[j]->stat.freqnb ]);
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
void printlog(wfb_utils_init_t *u, wfb_net_init_t *n) {

  uint8_t template[]="devraw(%d) freqnb(%d) mainraw(%d) backraw(%d) incom(%d) fails(%d) sent(%d)\n";
  wfb_utils_log_t *plog = &u->log;
  for (uint8_t i=0; i < n->nbraws; i++) {
    wfb_net_status_t *pst = &(n->rawdevs[i]->stat);
    plog->len += sprintf((char *)plog->txt + plog->len, (char *)template,
                          i, pst->freqnb, n->rawchan.mainraw, n->rawchan.backraw, pst->incoming,
                          pst->fails, pst->sent);
  }
  sendto(plog->fd, plog->txt, plog->len, 0,  (const struct sockaddr *)&plog->addr, sizeof(struct sockaddr));
  plog->len = 0;
}


/*****************************************************************************/
void wfb_utils_periodic(wfb_utils_init_t *u, wfb_net_init_t *n) {
#if RAW
  setmainbackup(n);
  printlog(u,n);
#endif  // RAW
}

/*****************************************************************************/
void wfb_utils_addraw(wfb_utils_init_t *pu, wfb_net_init_t *pn) {

  for (uint8_t i=0;i < pn->nbraws; i++) {
    pu->readtab[pu->readnb] = WFB_NB; pu->socktab[WFB_NB] = pu->readnb;
    pu->fd[pu->readnb] = pn->rawdevs[i]->sockfd;
    pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;
  }
}
#else
void wfb_utils_noraw(wfb_utils_init_t *pu) {

  pu->readtab[pu->readnb] = WFB_NB; pu->socktab[WFB_NB] = pu->readnb;
  if (-1 == (pu->fd[pu->readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(pu->fd[pu->readnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in norawinaddr;
  norawinaddr.sin_family = AF_INET;
  norawinaddr.sin_port = htons(PORT_NORAW);
  norawinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL_RAW);
  if (-1 == bind( pu->fd[pu->readnb], (const struct sockaddr *)&norawinaddr, sizeof(norawinaddr))) exit(-1);
  pu->norawoutaddr.sin_family = AF_INET;
  pu->norawoutaddr.sin_port = htons(PORT_NORAW);
  pu->norawoutaddr.sin_addr.s_addr = inet_addr(IP_REMOTE_RAW);
  pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;
}
#endif // RAW

/*****************************************************************************/
void wfb_utils_init(wfb_utils_init_t *pu) {

  if (-1 == (pu->log.fd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  pu->log.addr.sin_family = AF_INET;
  pu->log.addr.sin_port = htons(PORT_LOG);
  pu->log.addr.sin_addr.s_addr = inet_addr(IP_LOCAL);

  pu->readtab[pu->readnb] = WFB_TIM; pu->socktab[WFB_TIM] = pu->readnb;
  if (-1 == (pu->fd[pu->readnb] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(pu->fd[pu->readnb], 0, &period, NULL);
  pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;

  pu->readtab[pu->readnb] = WFB_TUN; pu->socktab[WFB_TUN] = pu->readnb;
  struct ifreq ifr; memset(&ifr, 0, sizeof(struct ifreq));
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
  if (0 > (pu->fd[pu->readnb] = open("/dev/net/tun",O_RDWR))) exit(-1);
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (ioctl(pu->fd[pu->readnb], TUNSETIFF, &ifr ) < 0 ) exit(-1);
  uint16_t fd_tun_udp;
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

#if TELEM
  pu->readtab[pu->readnb] = WFB_TEL; pu->socktab[WFB_TEL] = pu->readnb;
#if BOARD
  if (-1 == (pu->fd[pu->readnb] = open( UART, O_RDWR | O_NOCTTY | O_NONBLOCK))) exit(-1);
  struct termios tty;
  if (0 != tcgetattr(pu->fd[pu->readnb], &tty)) exit(-1);
  cfsetispeed(&tty,B115200);
  cfsetospeed(&tty,B115200);
  cfmakeraw(&tty);
  if (0 != tcsetattr(pu->fd[pu->readnb], TCSANOW, &tty)) exit(-1);
  tcflush(pu->fd[pu->readnb] ,TCIFLUSH);
  tcdrain(pu->fd[pu->readnb]);
#else
  if (-1 == (pu->fd[pu->readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(pu->fd[pu->readnb] , SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in telinaddr;
  telinaddr.sin_family = AF_INET;
  telinaddr.sin_port = htons(PORT_TELUP);
  telinaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);
  if (-1 == bind( pu->fd[pu->readnb], (const struct sockaddr *)&telinaddr, sizeof(telinaddr))) exit(-1);
  pu->teloutaddr.sin_family = AF_INET;
  pu->teloutaddr.sin_port = htons(PORT_TELDOWN);
  pu->teloutaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);
#endif // BOARD
  pu->readsets[pu->readnb].fd = pu->fd[pu->readnb]; pu->readsets[pu->readnb].events = POLLIN; pu->readnb++;
#endif // TELEM

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
