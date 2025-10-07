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
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <linux/if_tun.h>

#include "zfex.h"

//typedef enum { WFB_TIM, WFB_RAW, WFB_TUN, WFB_VID, WFB_NB } type_d;
typedef enum { WFB_RAW, WFB_VID, WFB_NB } type_d;

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

#define TUN_MTU 1400
#define TUNIP_BOARD     "10.0.1.2"
#define TUNIP_GROUND    "10.0.1.1"
#define IPBROAD         "255.255.255.0"

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
  uint8_t readtab[WFB_NB],socktab[WFB_NB];
  uint8_t readnb=0;

  readtab[readnb] = WFB_TIM; socktab[WFB_TIM] = readnb;
  if (-1 == (fd[readnb] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[readnb], 0, &period, NULL);
  readsets[readnb].fd = fd[readnb]; readsets[readnb].events = POLLIN; readnb++;

  readtab[readnb] = WFB_RAW; socktab[WFB_RAW] = readnb;
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
  readsets[readnb].fd = fd[readnb]; readsets[readnb].events = POLLIN; readnb++;
/*
  readtab[readnb] = WFB_TUN; socktab[WFB_TUN] = readnb;
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
  if (0 > (*fd = open("/dev/net/tun",O_RDWR))) exit(-1);
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (ioctl(*fd, TUNSETIFF, &ifr ) < 0 ) exit(-1);
  if (-1 == (fd[readnb] = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP))) exit(-1);
  addr.sin_family = AF_INET;
  memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));
  if (ioctl( fd[readnb], SIOCSIFADDR, &ifr ) < 0 ) exit(-1);
  addr.sin_addr.s_addr = inet_addr(IPBROAD);
  memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));
  if (ioctl( fd[readnb], SIOCSIFNETMASK, &ifr ) < 0 ) exit(-1);
  dstaddr.sin_family = AF_INET;
  memcpy(&ifr.ifr_addr, &dstaddr, sizeof(struct sockaddr));
  if (ioctl( fd[readnb], SIOCSIFDSTADDR, &ifr ) < 0 ) exit(-1);
  ifr.ifr_mtu = TUN_MTU;
  if (ioctl( fd[readnb], SIOCSIFMTU, &ifr ) < 0 ) exit(-1);
  ifr.ifr_flags = IFF_UP ;
  if (ioctl( fd[readnb], SIOCSIFFLAGS, &ifr ) < 0 ) exit(-1);
  readsets[readnb].fd = fd[readnb]; readsets[readnb].events = POLLIN; readnb++;
*/
  readtab[readnb] = WFB_VID; socktab[WFB_VID] = readnb;
  if (-1 == (fd[readnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
#if BOARD
  if (-1 == setsockopt(fd[readnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in  vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(PORT_VID);
  vidinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL);
  if (-1 == bind( fd[readnb], (const struct sockaddr *)&vidinaddr, sizeof( vidinaddr))) exit(-1);
  readsets[readnb].fd = fd[readnb]; readsets[readnb].events = POLLIN; readnb++;
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
//  uint64_t exptime;

  ssize_t len;
  ssize_t vidlen=0;

  ssize_t dumlen=0;

//  uint8_t tunbuf[ONLINE_MTU];
//  ssize_t tunlen=0;

  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
  uint8_t rawcur=0;;

#if BOARD
  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;;
#else
  unsigned index[FEC_K];
  uint8_t *inblocks[FEC_K+1];
  uint8_t *outblocks[FEC_N-FEC_K];
  uint8_t outblockrecov[FEC_N-FEC_K];
  uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];

  uint8_t inblocksnb=0;
  uint8_t recovcpt=0;

  int8_t inblockstofec=-1;

  int8_t failfec=-1;

  uint8_t msginnxtfec=0;

  int16_t msginnxtseq=-1;
  int16_t msgincurseq=-1;

  bool bypassflag = false;
  bool clearflag = false;
#endif

  for(;;) {
    if (0 != poll(readsets, readnb, -1)) {

      for (uint8_t cpt=0; cpt<readnb; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (readtab[cpt] == WFB_TIM )  { len = read(fd[socktab[WFB_TIM]], &exptime, sizeof(uint64_t)); printf("Click\n"); }
          if (readtab[cpt] == WFB_RAW) {
					   
            wfb_utils_heads_pay_t headspay;
            memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
            memset(&rawbuf[rawcur][0],0,ONLINE_MTU);

            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };
            struct iovec iovtab[2] = {iovheadpay, iovpay};

            struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2 };
            len = recvmsg(fd[socktab[WFB_RAW]], &msg, MSG_DONTWAIT);

            if (len > 0) {
//            if( headspay.msgcpt == WFB_TUN) len = write(fd[socktab[WFB_TUN]], iovpay.iov_base, iovpay.iov_len);
#if BOARD
#else
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
                        fec_decode(fec_p,
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
                    vidlen = ((wfb_utils_fec_t *)ptr)->feclen - sizeof(wfb_utils_fec_t);
                    ptr += sizeof(wfb_utils_fec_t);
                    vidlen = sendto(fd[socktab[WFB_VID]], ptr, vidlen, MSG_DONTWAIT, (struct sockaddr *)&vidoutaddr, sizeof(vidoutaddr));
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
	      } // headspay.msgcpt == WFB_VID
#endif // BOARD
            }
	  } // readtab[cpt] == WFB_RAW
/*
          if (readtab[cpt] == WFB_TUN) { 
            memset(&tunbuf[0],0,ONLINE_MTU);
            struct iovec iov;
            iov.iov_base = &tunbuf[0];
	    iov.iov_len = ONLINE_MTU;
	    tunlen = readv( fd[socktab[WFB_TUN]], &iov, 1);
	    printf("tunlen(%ld)\n",tunlen);
	  }
*/
#if BOARD
          if (readtab[cpt] == WFB_VID) { 
            memset(&vidbuf[vidcur][0],0,ONLINE_MTU);
    	    struct iovec iov;
            iov.iov_base = &vidbuf[vidcur][sizeof(wfb_utils_fec_t)];
            iov.iov_len = PAY_MTU;
            vidlen = readv( fd[socktab[WFB_VID]], &iov, 1) + sizeof(wfb_utils_fec_t);
            ((wfb_utils_fec_t *)&vidbuf[vidcur][0])->feclen = vidlen;
      	    vidcur++;
	  }
#endif // BOARD
        } // readsets[cpt].revents == POLLIN 
      } // for 

#if BOARD
      uint8_t kmin,kmax;
      kmin=(vidcur-1);
      if (vidcur == FEC_K) {
        vidcur=0; kmax=FEC_N;
        unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
        uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)vidbuf[f];
        uint8_t *fecblocks[FEC_N-FEC_K];
        for (uint8_t f=0; f<(FEC_N - FEC_K); f++) fecblocks[f] = (uint8_t *)&vidbuf[f + FEC_K];
        fec_encode(fec_p,
                    (const gf*restrict const*restrict const)datablocks,
                    (gf*restrict const*restrict const)fecblocks,
                    (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);
      } else kmax=vidcur;
      if (vidlen>0) {
        for (uint8_t k=kmin;k<kmax;k++) {
          if (k<FEC_K) vidlen=((wfb_utils_fec_t *)&vidbuf[k][0])->feclen; else vidlen=ONLINE_MTU;
          struct iovec iovpay = { .iov_base = &vidbuf[k][0], .iov_len = vidlen };
#else
      if (dumlen>0) { uint8_t k=0, dumbuf[1];
          struct iovec iovpay = { .iov_base = &dumbuf, .iov_len = vidlen };
#endif // BOARD
          wfb_utils_heads_pay_t headspay =
            { .droneid = DRONEID, .msgcpt = WFB_VID, .msglen = vidlen, .seq = sequence, .fec = k, .num = num++ };
          struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
          struct iovec iovtab[2] = {iovheadpay, iovpay};
  	  struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2, .msg_name = &norawoutaddr, .msg_namelen = sizeof(norawoutaddr) };
          len = sendmsg(fd[socktab[WFB_RAW]], (const struct msghdr *)&msg, MSG_DONTWAIT);
#if BOARD
	  vidlen = 0;
          if ((vidcur == 0)&&(k == (FEC_N-1))) sequence++;
        } // for k 
#endif // BOARD
      }

    } // poll
  }
}
