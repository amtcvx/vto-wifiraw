#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <errno.h>

#include "wfb_utils.h"
#if RAW
#include "wfb_net.h"
#endif // RAW

/*****************************************************************************/
int main(void) {

  wfb_utils_init_t u;
  wfb_utils_init(&u);
  uint8_t minraw = u.readnb;
#if RAW
  int16_t probuf[MAXRAWDEV];
  ssize_t lentab[WFB_NB][MAXRAWDEV];
  wfb_net_init_t n;
  if (false == wfb_net_init(&n)) { printf("NO WIFI\n"); exit(-1); }
  wfb_utils_addraw(&u,&n);
#endif // RAW
  uint8_t maxraw = u.readnb; 

  uint8_t sequence=0;
  uint8_t num=0;
  uint64_t exptime;
  ssize_t len;
  uint8_t rawcur=0;;

  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
  uint8_t tunbuf[ONLINE_MTU];
#if BOARD
  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;;
#endif // BOARD


  for(;;) {
    if (0 != poll(u.readsets, u.readnb, -1)) {

      for (uint8_t cpt=0; cpt<u.readnb; cpt++) {
        if (u.readsets[cpt].revents == POLLIN) {

          if ( cpt == WFB_PRO )  { 
	    len = read(u.fd[WFB_PRO], &exptime, sizeof(uint64_t)); 
#if RAW
            wfb_utils_periodic(&u,&n,lentab,probuf);
#endif // RAW
	  } else {
            if ((cpt < minraw) && (n.rc.mainraw >= 0)) n.rawdevs[n.rc.mainraw]->stat.syncelapse = true;
	  }

	  if (cpt == WFB_TUN) {
            memset(&tunbuf[0],0,ONLINE_MTU);
            struct iovec iov; iov.iov_base = &tunbuf[0]; iov.iov_len = ONLINE_MTU;
            len = readv( u.fd[WFB_TUN], &iov, 1);
            if (n.rc.mainraw >= 0) lentab[WFB_TUN][n.rc.mainraw] = len; 
          }
#if BOARD
          if (cpt == WFB_VID) {
            memset(&vidbuf[vidcur][0],0,ONLINE_MTU);
    	    struct iovec iov;
            iov.iov_base = &vidbuf[vidcur][sizeof(wfb_utils_fechd_t)];
            iov.iov_len = PAY_MTU;
            lentab[WFB_VID][n.rc.mainraw] = readv( u.fd[WFB_VID], &iov, 1) + sizeof(wfb_utils_fechd_t);
            ((wfb_utils_fechd_t *)&vidbuf[vidcur][0])->feclen = lentab[WFB_VID][n.rc.mainraw];
      	    vidcur++;
	  }
#endif // BOARD
  
          if ((cpt >= minraw) && (cpt < maxraw)) {
            wfb_utils_heads_pay_t headspay;
            memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
            memset(&rawbuf[rawcur][0],0,ONLINE_MTU);
            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };
#if RAW
            struct iovec iovtab[5] = { iov_radiotaphd_rx, iov_ieeehd_rx, iov_llchd_rx, iovheadpay, iovpay }; uint8_t msglen = 5;
	    memset(iov_llchd_rx.iov_base, 0, sizeof(iov_llchd_rx));
#endif // RAW
            struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = msglen };
            len = recvmsg(u.fd[cpt], &msg, MSG_DONTWAIT) - sizeof(wfb_utils_heads_pay_t);
#if RAW
            if (!((len > 0) &&
#if BOARD
              (headspay.droneid == DRONEID_GRD)
#else // BOARD
              (headspay.droneid >= DRONEID_MIN)&&(headspay.droneid <= DRONEID_MAX)
#endif // BOARD
              && (((uint8_t *)iov_llchd_rx.iov_base)[0]==1)&&(((uint8_t *)iov_llchd_rx.iov_base)[1]==2)
              && (((uint8_t *)iov_llchd_rx.iov_base)[2]==3)&&(((uint8_t *)iov_llchd_rx.iov_base)[3]==4))) {
	        (n.rawdevs[cpt-minraw]->stat.synccum)++;
              } else {
#endif // RAW
                if( headspay.msgcpt == WFB_TUN) len = write(u.fd[WFB_TUN], iovpay.iov_base, len);
#if RAW
#if BOARD
#else // BOARD
                uint8_t rawcpt = cpt - minraw;
                if (n.rawdevs[rawcpt]->stat.syncchan != headspay.chan) {
                  n.rawdevs[rawcpt]->stat.syncchan = headspay.chan;
		  if (headspay.chan != 0) wfb_utils_syncground(&u, &n, rawcpt);
		}

                if( headspay.msgcpt == WFB_VID) {
                  if (rawcur < (MAXNBRAWBUF-1)) rawcur++; else rawcur=0; 
		  wfb_utils_sendfec(&u.fec, u.fec_p, headspay.seq, headspay.fec, iovpay.iov_base);
	        } 
#endif // BOARD
#endif // RAW
	    } 
	  }
        } 
      } 

      uint8_t kmin = 0, kmax = 1;
#if BOARD
      if (lentab[WFB_VID][n.rc.mainraw] > 0) {
        kmin=(vidcur-1);
        if (vidcur == FEC_K) {
          vidcur=0; kmax=FEC_N;
          unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
          uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)vidbuf[f];
          uint8_t *fecblocks[FEC_N-FEC_K];
          for (uint8_t f=0; f<(FEC_N - FEC_K); f++) fecblocks[f] = (uint8_t *)&vidbuf[f + FEC_K];
          fec_encode(u.fec_p,
                      (const gf*restrict const*restrict const)datablocks,
                      (gf*restrict const*restrict const)fecblocks,
                      (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);
        } else kmax=vidcur;
      }
#endif // BOARD
      for (uint8_t k=kmin;k<kmax;k++) {
        for (uint8_t d=0; d < WFB_NB; d++) {
          for (uint8_t c = 0; c < (maxraw - minraw); c++) {
            if (lentab[d][c] > 0) {
              struct iovec iovpay;
#if BOARD
#if RAW
              if (d == WFB_PRO) {
                lentab[WFB_PRO][c] = 0; iovpay.iov_len = 0;
                //iovpay.iov_base = &probuf[c]; 
              }
#endif // RAW
#endif // BOARD
              if (d == WFB_TUN) {
                iovpay.iov_base = &tunbuf; iovpay.iov_len = lentab[WFB_TUN][c];
              };
#if BOARD
              if (d == WFB_VID) {
                if (k<FEC_K) lentab[WFB_VID][n.rc.mainraw]=((wfb_utils_fechd_t *)&vidbuf[k][0])->feclen; else lentab[WFB_VID][n.rc.mainraw]=ONLINE_MTU;
                iovpay.iov_base = &vidbuf[k][0]; iovpay.iov_len = lentab[WFB_VID][n.rc.mainraw];
	      }
#endif // BOARD
              wfb_utils_heads_pay_t headspay =
                { .chan = probuf[c], .droneid = DRONEID, .msgcpt = d, .msglen = lentab[d][c], .seq = sequence, .fec = k, .num = num++ };
              struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
#if RAW
              struct iovec iovtab[5] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovheadpay, iovpay }; uint8_t msglen = 5;
	      struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = msglen };
#endif // RAW
              len = sendmsg(u.fd[ c + minraw ], (const struct msghdr *)&msg, MSG_DONTWAIT);
#if RAW
	      n.rawdevs[c]->stat.sent++;
#endif // RAW
	      lentab[d][c] = 0;
#if BOARD
              if ((d == WFB_VID)&&(vidcur == 0)&&(k == (FEC_N-1))) sequence++;
#endif // BOARD
	    }
	  }
        }
      }
    } // poll
  }
}
