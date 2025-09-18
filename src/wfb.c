#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/uio.h>
#include <sys/timerfd.h>

#include "zfex.h"

typedef enum { WFB_TIM, WFB_RAW, WFB_VID, WFB_NB } type_d;

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

#define PAY_MTU 1400

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)

#define MAXNBRAWBUF 2*FEC_N

#define MAXNBDEV 25 

#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

#define PORT_NORAW  3000
#define PORT_VID  5600


#define DRONEID_GRD 0
#define DRONEID_MIN 1
#define DRONEID_MAX 2

#if BOARD
#define DRONEID 1
#else
#define DRONEID DRONEID_GRD
#endif // BOARD


#define FEC_K   8
#define FEC_N   12

/*****************************************************************************/
int main(void) {

  struct pollfd readsets[MAXNBDEV];
  uint8_t fd[MAXNBDEV];
  uint8_t readtab[WFB_NB];
  uint8_t readnb=0;

  readtab[readnb] = WFB_TIM;
  if (-1 == (fd[readnb] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[readnb], 0, &period, NULL);
  readsets[readnb].fd = fd[readnb];
  readsets[readnb].events = POLLIN;
  readnb++;

  readtab[readnb] = WFB_RAW;
  if (-1 == (fd[readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(fd[readnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in norawinaddr;
  norawinaddr.sin_family = AF_INET;
  norawinaddr.sin_port = htons(PORT_NORAW);
  norawinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL_RAW);
  if (-1 == bind( fd[readnb], (const struct sockaddr *)&norawinaddr, sizeof(norawinaddr))) exit(-1);
  struct sockaddr_in norawoutaddr;
  norawoutaddr.sin_family = AF_INET;
  norawoutaddr.sin_port = htons(PORT_NORAW);
  norawoutaddr.sin_addr.s_addr = inet_addr(IP_REMOTE_RAW);
  readsets[readnb].fd = fd[readnb];
  readsets[readnb].events = POLLIN;
  readnb++;

  readtab[readnb] = WFB_VID;
  if (-1 == (fd[readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
#if BOARD
  if (-1 == setsockopt(fd[readnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in  vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(PORT_VID);
  vidinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL);
  if (-1 == bind( fd[readnb], (const struct sockaddr *)&vidinaddr, sizeof( vidinaddr))) exit(-1);
  readsets[readnb].fd = fd[readnb];
  readsets[readnb].events = POLLIN;
  readnb++;
#else
  struct sockaddr_in vidoutaddr;
  vidoutaddr.sin_family = AF_INET;
  vidoutaddr.sin_port = htons(PORT_VID);
  vidoutaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);
#endif // BOARD

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);
  uint8_t sequence=0;
  uint8_t num=0;
  uint64_t exptime;

  ssize_t len;

  ssize_t rawlen;
  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
  uint8_t rawcur=0;;

  uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];
  uint8_t *outblocks[FEC_N-FEC_K];
  uint8_t outblocksidx = 0;
  uint8_t *inblocks[FEC_K];
  uint8_t *inblocksto; 
  uint8_t index[FEC_K];
  uint8_t fecsto;
  uint8_t msgincurseq=0;
  uint8_t msginnxtseq=0;
  uint8_t msginnxtfec=0;
  bool msginfails = false;

  ssize_t vidlen=0;
  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;;

  for(;;) {
    if (0 != poll(readsets, readnb, -1)) {
      for (uint8_t cpt=0; cpt<readnb; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (readtab[cpt] == WFB_TIM ) { // TIMER
	    len = read(fd[cpt], &exptime, sizeof(uint64_t)); printf("Click\n");
	  } else {
            if (readtab[cpt] == WFB_RAW) { // NORAW
              wfb_utils_heads_pay_t headspay;
              memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));

              struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
              struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };
              struct iovec iovtab[2] = {iovheadpay, iovpay};

              struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2 };
              len = recvmsg(fd[cpt], &msg, MSG_DONTWAIT);

              if (len > 0) {
//              if( headspay.msgcpt == WFB_TUN) {
//                len = write(utils.fd[utils.nbraws + 1], piovpay->iov_base, piovpay->iov_len);
//              }
#if BOARD
#else
                if( headspay.msgcpt == WFB_VID) {


		   if ((headspay.seq==1)&&(headspay.fec==2)) {
                     printf("\nMISSING (%d)(%d)\n",headspay.seq,headspay.fec);
    	             printf("len(%d)  ",headspay.msglen);
    	             for (uint8_t i=0;i<5;i++) printf("%x ",*(uint8_t *)(iovpay.iov_base + i));printf(" ... ");
    	             for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",*(uint8_t *)(iovpay.iov_base + i));printf("\n");
		     break;
		   }


                  bool clearflag=false;
                  struct iovec iovrecover[FEC_N-FEC_K];
                  uint8_t imax=0, imin=0;
                  if ((msginnxtseq != headspay.seq)||(msginnxtfec != headspay.fec)) {
                    if (headspay.fec < (FEC_N-1)) { msginnxtfec=(headspay.fec+1); msginnxtseq=headspay.seq; }
                    else {
                      msginnxtfec=0;
                      if (headspay.seq < 254) msginnxtseq=(headspay.seq+1); else msginnxtseq=0;
                    }
                    printf("KO\n");
                    msginfails = true;
                  } else {
                    printf("OK\n");
                    if (msginnxtfec < (FEC_N-1)) msginnxtfec++;
                    else { msginnxtfec=0; if (msginnxtseq < 255) msginnxtseq++; else msginnxtseq=0; }
                    if (headspay.fec < FEC_K) {imin=headspay.fec; imax=(1+imin); }
                  }

                  if (msgincurseq == headspay.seq) {
                    if (headspay.fec < FEC_K) {
		      inblocks[headspay.fec] = (uint8_t *)&iovpay.iov_base; 
		      index[headspay.fec] = headspay.fec;
		    } else {
                      for(uint8_t k=0;k<FEC_K;k++) {
                        if (!(inblocks[k])) {
                          inblocks[k]=(uint8_t *)&iovpay.iov_base;
                          index[k]=k;
			  outblocks[outblocksidx] = &outblocksbuf[outblocksidx][0]; 
			  outblocksidx++;
			}
		      }
		    }
		  } else { inblocksto = (uint8_t *)&iovpay.iov_base; fecsto = headspay.fec; }

                  if (rawcur < (MAXNBRAWBUF-1)) rawcur++; else rawcur=0;

                  if (msgincurseq != headspay.seq) {
                    msgincurseq = headspay.seq;

                    if (msginfails) {
                      msginfails = false;

		      if ((outblocksidx > 0)&&(outblocksidx <= (FEC_N - FEC_K))) {
                        printf("\nDECODE (%d)\n",outblocksidx);
                        fec_decode(fec_p,
                           (const unsigned char **)inblocks,
                           (unsigned char * const*)outblocks,
                           (unsigned int *)index,
                           ONLINE_MTU);

			for (uint8_t i=0;i<=outblocksidx;i++) {
                          vidlen = ((wfb_utils_fec_t *)&outblocksbuf[i][0]);
			  printf("len(%ld)  ",vidlen);
                          for (uint8_t i=0;i<5;i++) printf("%x ",outblocksbuf[i][0]);printf(" ... ");
                          for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",outblocksbuf[i][0]); printf("\n");
			}
		      }
		    }
		  }

                  for (uint8_t i=imin;i<imax;i++) {
                    inblocks[fecsto]=inblocksto;

		     vidlen = ((wfb_utils_fec_t *)iovpay.iov_base)->feclen;
                    
		     if ((len = sendto(fd[2], iovpay.iov_base + sizeof(wfb_utils_fec_t),
                                    vidlen - sizeof(wfb_utils_fec_t), MSG_DONTWAIT,
                                    (struct sockaddr *)&vidoutaddr, sizeof(vidoutaddr))) > 0) printf("len(%ld)\n",len);
		  }
                  imax=0; imin=0;

                  if (clearflag) {
                    clearflag=false;
                    memset(inblocks, 0, sizeof(inblocks));
                    memset(outblocks, 0, sizeof(outblocks));
                    memset(index, 0, sizeof(index));
		    outblocksidx=0;
		    clearflag=true;
		  }
		}
#endif // BOARD
	      }
	    } else {
#if BOARD
              if (readtab[cpt] == WFB_VID) { // VID
                memset(&vidbuf[vidcur][0],0,ONLINE_MTU);
    	        struct iovec iov;
                iov.iov_base = &vidbuf[vidcur][sizeof(wfb_utils_fec_t)];
                iov.iov_len = PAY_MTU;
                vidlen = readv( fd[cpt], &iov, 1) + sizeof(wfb_utils_fec_t);
                ((wfb_utils_fec_t *)&vidbuf[vidcur][0])->feclen = vidlen;
      	        vidcur++;
/*
    	        printf("(%d)len(%ld)  ",vidcur-1,vidlen);
    	        for (uint8_t i=0;i<5;i++) printf("%x ",vidbuf[vidcur-1][i]);printf(" ... ");
    	        for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",vidbuf[vidcur-1][i]);printf("\n");
*/
	      }
#endif // BOARD
	    }
          }
        }

        uint8_t kmin,kmax; 

#if BOARD
	kmin=(vidcur-1);
        if (vidcur == FEC_K) {
	  vidcur=0; kmax=FEC_N;
          unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
          uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)&vidbuf[f];
          uint8_t *fecblocks[FEC_N-FEC_K];
          for (uint8_t f=0; f<(FEC_N - FEC_K); f++) {
            fecblocks[f] = (uint8_t *)&vidbuf[f + FEC_K];
          }
          fec_encode(fec_p,
                    (const gf*restrict const*restrict const)datablocks,
                    (gf*restrict const*restrict const)fecblocks,
                    (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);
        } else kmax=vidcur;
#endif // BOARD
        if (vidlen>0) {
          for (uint8_t k=kmin;k<kmax;k++) {
            if (k>=FEC_K)  vidlen = ONLINE_MTU;
            wfb_utils_heads_pay_t headspay =
            { .droneid = DRONEID, .msgcpt = WFB_VID, .msglen = vidlen, .seq = sequence, .fec = k, .num = num++ };
            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovpay = { .iov_base = &vidbuf[k][0], .iov_len = vidlen };
            struct iovec iovtab[2] = {iovheadpay, iovpay};
  	    struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2, .msg_name = &norawoutaddr, .msg_namelen = sizeof(norawoutaddr) };
            len = sendmsg(fd[WFB_RAW], (const struct msghdr *)&msg, MSG_DONTWAIT);
/*
            printf("(%d)>>len(%ld)  ",k,vidlen);
            for (uint8_t i=0;i<5;i++) printf("%x ",vidbuf[k][i]);printf(" ... ");
            for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",vidbuf[k][i]);printf("\n");
*/
	  }
	  vidlen = 0;
	  if (vidcur==0) sequence++;
	}
      }
    }
  }
}
