#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <errno.h>

#include "wfb_utils.h"

#if RAW
#include "wfb_net.h"
#else 
#define MAXRAWDEV 1
typedef struct {
  int8_t mainraw;
} noraw_rawchan_t;
typedef struct {
  uint8_t nbraws;
  noraw_rawchan_t rc;
} noraw_init_t;

#endif // RAW

/*****************************************************************************/
int main(void) {

  wfb_utils_init_t u;
  wfb_utils_init(&u);
  int16_t probuf[MAXRAWDEV];
  ssize_t lentab[WFB_NB][MAXRAWDEV];
#if RAW
  wfb_net_init_t n;
  if (false == wfb_net_init(&n)) { printf("NO WIFI\n"); exit(-1); }
  wfb_utils_addraw(&u,&n);
  for (uint8_t i=0;i<n.nbraws;i++) printf("(%s)\n",n.rawdevs[i]->ifname);
#else
  probuf[0]=0;
  wfb_utils_addnoraw(&u);
  noraw_init_t n = { .nbraws = 1, .rc = { .mainraw = 0} };
#endif // RAW

  uint8_t sequence=0;
  uint8_t num=0;
  uint64_t exptime;
  ssize_t len;
  uint8_t rawcur=0;;

  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
#if TELEM
  uint8_t buf[2][ONLINE_MTU];
#else
  uint8_t buf[1][ONLINE_MTU];
#endif // TELEM
#if BOARD
  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;;
#endif // BOARD

  for(;;) {
    if (0 != poll(u.readsets, u.readnb, -1)) {
      for (uint8_t cpt=0; cpt<u.readnb; cpt++) {
        if (u.readsets[cpt].revents == POLLIN) {
          uint8_t cptid =  u.readtab[cpt];

	  if ( cptid == WFB_PRO ) {
	    len = read(u.readsets[cpt].fd, &exptime, sizeof(uint64_t)); 
#if RAW
            wfb_utils_periodic(&u,&n,lentab,probuf);
	  } else {
            if ((n.rc.mainraw >= 0) && ( cptid < WFB_NB ))  n.rawdevs[n.rc.mainraw]->stat.syncelapse = true;
#endif // RAW
          }

	  if (( cptid == WFB_TUN )
#if TELEM
	    || ( cptid == WFB_TEL)
#endif // TELEM
	    ) {
            memset(&buf[cptid-WFB_TUN][0],0,ONLINE_MTU);
            struct iovec iov; iov.iov_base = &buf[cptid-WFB_TUN][0]; iov.iov_len = ONLINE_MTU;
            len = readv( u.readsets[cpt].fd, &iov, 1);
            if (n.rc.mainraw >= 0) lentab[cptid][n.rc.mainraw] = len; 
          }

#if BOARD
          if (cptid == WFB_VID) {
            memset(&vidbuf[vidcur][0],0,ONLINE_MTU);
    	    struct iovec iov;
            iov.iov_base = &vidbuf[vidcur][sizeof(wfb_utils_fechd_t)];
            iov.iov_len = PAY_MTU;
            lentab[WFB_VID][n.rc.mainraw] = readv( u.readsets[cpt].fd, &iov, 1) + sizeof(wfb_utils_fechd_t);
            ((wfb_utils_fechd_t *)&vidbuf[vidcur][0])->feclen = lentab[WFB_VID][n.rc.mainraw];
      	    vidcur++;
	  }
#endif // BOARD

          if ( cptid >= WFB_NB ) { 
            wfb_utils_heads_pay_t headspay;
            memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
            memset(&rawbuf[rawcur][0],0,ONLINE_MTU);
            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };

#if RAW
            struct iovec iovtab[5] = { iov_radiotaphd_rx, iov_ieeehd_rx, iov_llchd_rx, iovheadpay, iovpay }; uint8_t tablen = 5;
	    memset(iov_llchd_rx.iov_base, 0, sizeof(iov_llchd_rx));
#else 
            struct iovec iovtab[2] = { iovheadpay, iovpay }; uint8_t tablen = 2;
#endif // RAW
            struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = tablen };
            len = recvmsg(u.readsets[cpt].fd, &msg, MSG_DONTWAIT) - sizeof(wfb_utils_heads_pay_t);

#if RAW
            if (!((len > 0) &&
#if BOARD
              (headspay.droneid == DRONEID)
//              (headspay.droneid == DRONEID_GRD)
#else // BOARD
//              (headspay.droneid >= DRONEID_MIN)&&(headspay.droneid <= DRONEID_MAX)
              (headspay.droneid == DRONEID)
#endif // BOARD
              && (((uint8_t *)iov_llchd_rx.iov_base)[0]==1)&&(((uint8_t *)iov_llchd_rx.iov_base)[1]==2)
              && (((uint8_t *)iov_llchd_rx.iov_base)[2]==3)&&(((uint8_t *)iov_llchd_rx.iov_base)[3]==4))) {

	      (n.rawdevs[cptid - WFB_NB]->stat.synccum)++;
	      headspay.msglen = 0;

            } else {
#if BOARD
#else // BOARD
              uint8_t rawcpt = cptid - WFB_NB;
              if (n.rawdevs[rawcpt]->stat.syncchan != headspay.chan) {
                n.rawdevs[rawcpt]->stat.syncchan = headspay.chan;
                if (headspay.chan != 0) wfb_utils_syncground(&u, &n, rawcpt);
              }
#endif // BOARD
            }
#endif // RAW

            if (headspay.msglen > 0) {

              if (headspay.msgcpt == WFB_TUN) len = write(u.fd[WFB_TUN], iovpay.iov_base, headspay.msglen);
#if TELEM
              if (headspay.msgcpt == WFB_TEL)
#if BOARD
                len = write(u.fd[WFB_TEL], iovpay.iov_base, headspay.msglen);
#else // BOARD
                len = sendto(u.fd[WFB_TEL], iovpay.iov_base, headspay.msglen, MSG_DONTWAIT,  (struct sockaddr *)&(u.teloutaddr), sizeof(struct sockaddr));
#endif // BOARD
#endif // TELEM
       
#if BOARD
#else // BOARD
              if( headspay.msgcpt == WFB_VID) {
                if (rawcur < (MAXNBRAWBUF-1)) rawcur++; else rawcur=0; 
	        wfb_utils_sendfec(&u.fec, u.fec_p, headspay.seq, headspay.fec, iovpay.iov_base);
	      } 
#endif // BOARD
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
          for (uint8_t c = 0; c < n.nbraws ; c++) {
            if (lentab[d][c] > 0) {
              struct iovec iovpay;
#if BOARD
#if RAW
              if (d == WFB_PRO) {
                lentab[WFB_PRO][c] = 0; iovpay.iov_len = 0;
              }
#endif // RAW
#endif // BOARD
              if ((d == WFB_TUN) 
#if TELEM
                || (d == WFB_TEL)
#endif // TELEM
              ) {
                iovpay.iov_base = &buf[d-WFB_TUN][0]; iovpay.iov_len = lentab[d][c];
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
#else 
              struct iovec iovtab[2] = { iovheadpay, iovpay }; uint8_t msglen = 2;
              struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = msglen, .msg_name = &u.norawoutaddr, .msg_namelen = sizeof(u.norawoutaddr) };
#endif // RAW
              len = sendmsg(u.fd[ c + WFB_NB ], (const struct msghdr *)&msg, MSG_DONTWAIT);
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
