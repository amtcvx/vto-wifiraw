#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <termios.h>

#include "wfb_utils.h"


typedef struct {
  uint8_t droneid;
  uint8_t msgcpt;
  uint16_t msglen;
  uint8_t seq;
  uint8_t fec;
  uint8_t num;
  uint8_t dum;
} __attribute__((packed)) wfb_utils_heads_pay_t;

typedef struct {
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fec_t;


/*****************************************************************************/
int main(void) {

  wfb_utils_init_t u;
  wfb_utils_init(&u);

  uint8_t sequence=0;
  uint8_t num=0;
  uint64_t exptime;
  ssize_t len;
  uint8_t rawcur=0;;

  ssize_t lentab[WFB_NB];

  uint8_t tunbuf[ONLINE_MTU];
  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
#if TELEM
  uint8_t telbuf[ONLINE_MTU];
#endif // TELEM

#if BOARD
  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;;
#else
  uint8_t outblockrecov[FEC_N-FEC_K];
  uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];

  uint8_t *outblocks[FEC_N-FEC_K];
  unsigned index[FEC_K];
  uint8_t *inblocks[FEC_K+1];
  uint8_t inblocksnb=0;
  uint8_t recovcpt=0;
  int8_t inblockstofec=-1;
  int8_t failfec=-1;
  uint8_t msginnxtfec=0;
  int16_t msginnxtseq=-1;
  int16_t msgincurseq=-1;
  bool bypassflag = false;
  bool clearflag = false;
#endif // BOARD

  for(;;) {
    if (0 != poll(u.readsets, u.readnb, -1)) {

      for (uint8_t cpt=0; cpt<u.readnb; cpt++) {
        if (u.readsets[cpt].revents == POLLIN) {

          if (u.readtab[cpt] == WFB_TIM )  { len = read(u.fd[u.socktab[WFB_TIM]], &exptime, sizeof(uint64_t)); 
            u.log.len += sprintf((char *)&u.log.txt + u.log.len, "Click\n");
            sendto(u.log.fd, u.log.txt, u.log.len, MSG_DONTWAIT,  (const struct sockaddr *)&u.log.addr, sizeof(struct sockaddr));
	    u.log.len = 0;
	  }

          if (u.readtab[cpt] == WFB_TUN) { memset(&tunbuf[0],0,ONLINE_MTU); 
            struct iovec iov; iov.iov_base = &tunbuf[0]; iov.iov_len = ONLINE_MTU;
	    lentab[WFB_TUN] = readv( u.fd[u.socktab[WFB_TUN]], &iov, 1);
	  }
#if TELEM
          if (u.readtab[cpt] == WFB_TEL) { memset(&telbuf[0],0,ONLINE_MTU);
            struct iovec iov; iov.iov_base = &telbuf[0]; iov.iov_len = ONLINE_MTU;
            lentab[WFB_TEL] = readv( u.fd[socktab[WFB_TEL]], &iov, 1);
          }
#endif // TELEM
       
#if BOARD
          if (u.readtab[cpt] == WFB_VID) { 
            memset(&vidbuf[vidcur][0],0,ONLINE_MTU);
    	    struct iovec iov;
            iov.iov_base = &vidbuf[vidcur][sizeof(wfb_utils_fec_t)];
            iov.iov_len = PAY_MTU;
            lentab[WFB_VID] = readv( u.fd[u.socktab[WFB_VID]], &iov, 1) + sizeof(wfb_utils_fec_t);
            ((wfb_utils_fec_t *)&vidbuf[vidcur][0])->feclen = lentab[WFB_VID];
      	    vidcur++;
	  }
#endif // BOARD
          if (u.readtab[cpt] == WFB_RAW) {
					   
            wfb_utils_heads_pay_t headspay;
            memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
            memset(&rawbuf[rawcur][0],0,ONLINE_MTU);

            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };
            struct iovec iovtab[2] = {iovheadpay, iovpay};

            struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2 };
            len = recvmsg(u.fd[u.socktab[WFB_RAW]], &msg, MSG_DONTWAIT) - sizeof(wfb_utils_heads_pay_t);

            if (len > 0) {
              if( headspay.msgcpt == WFB_TUN) len = write(u.fd[u.socktab[WFB_TUN]], iovpay.iov_base, len);
#if BOARD
#if TELEM
              if( headspay.msgcpt == WFB_TEL)  len = write(u.fd[u.socktab[WFB_TEL]], iovpay.iov_base, len);
#endif // TELEM
#else
#if TELEM
              if( headspay.msgcpt == WFB_TEL) len = sendto(u.fd[u.socktab[WFB_TEL]], iovpay.iov_base, len, MSG_DONTWAIT, 
	        (struct sockaddr *)&u.teloutaddr, sizeof(u.teloutaddr));
#endif // TELEM
              if( headspay.msgcpt == WFB_VID) {
                if (rawcur < (MAXNBRAWBUF-1)) rawcur++; else rawcur=0;
                if (headspay.fec < FEC_K) {
                  if (msgincurseq < 0) msgincurseq = headspay.seq;
                  if ((inblockstofec >= 0) && ((msginnxtseq != headspay.seq) || (msginnxtfec != headspay.fec))
                     && (failfec < 0)) { failfec = msginnxtfec; if (failfec == 0) bypassflag = false; }
                  if (headspay.fec < (FEC_K-1)) msginnxtfec = headspay.fec+1;
		  else { msginnxtfec = 0; if (headspay.seq < 255) msginnxtseq = headspay.seq+1; else msginnxtseq = 0; }
                }

                uint8_t imax=0, imin=0;
                if (msgincurseq == headspay.seq) {
                  if (headspay.fec < FEC_K) {
                    if ((failfec < 0) || ((failfec > 0) && (headspay.fec < failfec))) { imin = headspay.fec; imax = (imin+1); }
                    inblocks[headspay.fec] = iovpay.iov_base; index[headspay.fec] = headspay.fec; inblocksnb++;
                  } else {
                    for (uint8_t k=0;k<FEC_K;k++) if (!(inblocks[k])) {
                      inblocks[k] = iovpay.iov_base; index[k] = headspay.fec;
                      outblocks[recovcpt]=&outblocksbuf[recovcpt][0]; outblockrecov[recovcpt] = k; recovcpt++;
                      break;
                    }
                  }
                } else {
                  msgincurseq = headspay.seq;
                  inblocks[FEC_K] = iovpay.iov_base;
                  clearflag=true;
                  imin = FEC_K; imax = (FEC_K+1);
                  if (inblockstofec >= 0) {
                    if ((failfec == 0) && (!(bypassflag))) { imin = 0; imax = 0; }
                    if ((failfec > 0) || ((failfec == 0) && (bypassflag))) {
                      imin = failfec;
                      if ((recovcpt + inblocksnb) != (FEC_K-1))  { for (uint8_t k=0;k<recovcpt;k++) inblocks[ outblockrecov[k] ] = 0; }
                      else {
                        imin = outblockrecov[0];
                        fec_decode(u.fec_p,
                                   (const unsigned char **)inblocks,
                                   (unsigned char * const*)outblocks,
                                   (unsigned int *)index,
                                   ONLINE_MTU);
      
                        for (uint8_t k=0;k<recovcpt;k++) inblocks[ outblockrecov[k] ] = outblocks[k];
                      }
                    }
                  }
                }
      
                for (uint8_t i=imin;i<imax;i++) {
                  uint8_t *ptr=inblocks[i];
                  if (ptr) {
                    ssize_t vidlen = ((wfb_utils_fec_t *)ptr)->feclen - sizeof(wfb_utils_fec_t);
                    ptr += sizeof(wfb_utils_fec_t);
                    vidlen = sendto(u.fd[u.socktab[WFB_VID]], ptr, vidlen, MSG_DONTWAIT, (struct sockaddr *)&u.vidoutaddr, sizeof(u.vidoutaddr));
                  }
                }
      
                if (clearflag) {
                  clearflag=false;
                  if ((failfec == 0)&&(!(bypassflag))) bypassflag = true;
                  else failfec = -1;
                  msginnxtseq = headspay.seq;
                  inblocksnb=0; recovcpt=0;
                  memset(inblocks, 0, (FEC_K * sizeof(uint8_t *)));
                  inblockstofec = headspay.fec;
                  inblocks[inblockstofec] = inblocks[FEC_K];
		}
	      } 
#endif // BOARD
            }
	  } 
        } 
      } 

      uint8_t kmin = 0, kmax = 1;
#if BOARD
      if (lentab[WFB_VID] > 0) {
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
          if (lentab[d] > 0) {
            struct iovec iovpay;
            if (d == WFB_TUN) { iovpay.iov_base = &tunbuf; iovpay.iov_len = lentab[WFB_TUN]; };
#if TELEM
            if (d == WFB_TEL) { iovpay.iov_base = &telbuf; iovpay.iov_len = lentab[WFB_TEL]; };
#endif // TELEM
#if BOARD
            if (d == WFB_VID) {
              if (k<FEC_K) lentab[WFB_VID]=((wfb_utils_fec_t *)&vidbuf[k][0])->feclen; else lentab[WFB_VID]=ONLINE_MTU;
              iovpay.iov_base = &vidbuf[k][0]; iovpay.iov_len = lentab[WFB_VID];
	    }
#endif // BOARD
            wfb_utils_heads_pay_t headspay =
              { .droneid = DRONEID, .msgcpt = d, .msglen = lentab[d], .seq = sequence, .fec = k, .num = num++ };
            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovtab[2] = {iovheadpay, iovpay};
  	    struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2, .msg_name = &u.norawoutaddr, .msg_namelen = sizeof(u.norawoutaddr) };
            len = sendmsg(u.fd[u.socktab[WFB_RAW]], (const struct msghdr *)&msg, MSG_DONTWAIT);
	    lentab[d] = 0;
#if BOARD
            if ((d == WFB_VID)&&(vidcur == 0)&&(k == (FEC_N-1))) sequence++;
#endif // BOARD
	  }
        }
      }
    } // poll
  }
}
