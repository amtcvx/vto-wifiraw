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

typedef enum { WFB_PRO, WFB_TUN, WFB_TEL, WFB_VID, WFB_NB } type_d;

typedef struct {
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fec_t;

#define PAY_MTU 1400

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)

#define MAXRAWDEV 20

#define MAXDEV (1 + MAXRAWDEV)

#define PERIOD_DELAY_S  1

#define IP_LOCAL "127.0.0.1"

#define PORT_NORAW  3000
#define PORT_VID  5600

#define FEC_K   8
#define FEC_N   12

/*****************************************************************************/
int main(void) {

  struct pollfd readsets[MAXDEV];
  uint8_t fd[MAXDEV];

  if (-1 == (fd[0] = timerfd_create(CLOCK_MONOTONIC, 0))) exit(-1);
  readsets[0].fd = fd[0];
  readsets[0].events = POLLIN;
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(fd[0], 0, &period, NULL);

  uint8_t readtabnb=1;
  if (-1 == (fd[readtabnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(fd[readtabnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in norawinaddr;
  norawinaddr.sin_family = AF_INET;
  norawinaddr.sin_port = htons(PORT_NORAW);
  norawinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL_RAW);
  if (-1 == bind( fd[readtabnb], (const struct sockaddr *)&norawinaddr, sizeof(struct sockaddr_in))) exit(-1);
  struct sockaddr_in norawoutaddr;
  norawoutaddr.sin_family = AF_INET;
  norawoutaddr.sin_port = htons(PORT_NORAW);
  norawoutaddr.sin_addr.s_addr = inet_addr(IP_REMOTE_RAW);
  readsets[readtabnb].fd = fd[readtabnb];
  readsets[readtabnb].events = POLLIN;
  readtabnb += 1;

  if (-1 == (fd[readtabnb] = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(fd[readtabnb], SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(struct sockaddr_in))) exit(-1);
#if BOARD
  struct sockaddr_in vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(PORT_VID);
  vidinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL_RAW);
  if (-1 == bind( fd[readtabnb], (const struct sockaddr *)&vidinaddr, sizeof(struct sockaddr_in))) exit(-1);
  readsets[readtabnb].fd = fd[readtabnb];
  readsets[readtabnb].events = POLLIN;
  readtabnb += 1;
#else
  struct sockaddr_in vidoutaddr;
  vidoutaddr.sin_family = AF_INET;
  vidoutaddr.sin_port = htons(PORT_VID);
  vidoutaddr.sin_addr.s_addr = inet_addr(IP_REMOTE_RAW);
#endif // BOARD
  readsets[readtabnb].fd = fd[readtabnb];
  readsets[readtabnb].events = POLLIN;


  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);
  uint8_t buf_vid[FEC_N][ONLINE_MTU];
  struct iovec iov[FEC_N], *iovfec[FEC_N];
  uint8_t curr = 0;
  for (uint8_t k=0;k<FEC_N;k++) iov[k].iov_len = 0;
  struct iovec *piov;

  for(;;) {
    if (0 != poll(readsets, 1, -1)) {
      for (uint8_t cpt=0; cpt<readtabnb; cpt++) {
        if (readsets[cpt].revents == POLLIN) {
          if (cpt == 0) { // TIMER
            printf("Click\n");
	  } else {
            if (cpt == 1) { // NORAW
			    
      	      uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];
              uint8_t *outblocks[FEC_N-FEC_K];
              unsigned index[FEC_K];
              uint8_t *inblocks[FEC_K];
              uint8_t  alldata=0;
              uint8_t j=FEC_K;
              uint8_t idx = 0;
              for (uint8_t k=0;k<FEC_K;k++) {
                index[k] = 0;
                inblocks[k] = (uint8_t *)0;
                if (k < (FEC_N - FEC_K)) outblocks[k] = (uint8_t *)0;
                if ( iovfec[k] ) {
                  inblocks[k] = (uint8_t *)iovfec[k]->iov_base;
                  index[k] = k;
                  alldata |= (1 << k);
                } else {
                  for(;j < FEC_N; j++) {
                    if ( iovfec[j] ) {
                      inblocks[k] = (uint8_t *)iovfec[j]->iov_base;
                      outblocks[idx] = &outblocksbuf[idx][0]; idx++;
                      index[k] = j;
                      j++;
                      alldata |= (1 << k);
                      break;
                    }
                  }
                }
              }
      
      	      printf("\n");
              if ((alldata == 255)&&(idx > 0)&&(idx <= (FEC_N - FEC_K))) {
                for (uint8_t k=0;k<FEC_K;k++) printf("%d ",index[k]);
                printf("\nDECODE (%d)\n",idx);
                fec_decode(fec_p,
                           (const unsigned char **)inblocks,
                           (unsigned char * const*)outblocks,
                           (unsigned int *)index,
                           ONLINE_MTU);
      	      }
      
              printf("\nRESTORING\n");
              uint8_t x=0;
      	      struct iovec recover[FEC_N-FEC_K];
              for (uint8_t k=0;k<FEC_K;k++) {
                if (!(iovfec[k])) {
                  recover[x].iov_base = outblocks[x];
                  recover[x].iov_len = ((wfb_utils_fec_t *)outblocks[x])->feclen;
      	          iovfec[k] = &recover[x];
                  x++;
      	          piov = iovfec[k];
                  printf("len(%ld)  ",piov->iov_len - sizeof(wfb_utils_fec_t));
                  for (uint8_t i=0;i<5;i++) printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf(" ... ");
                  for (uint16_t i=piov->iov_len-5-sizeof(wfb_utils_fec_t);i<piov->iov_len-sizeof(wfb_utils_fec_t);i++)
                    printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf("\n");
      	        }
      	      }
	    } else {
#if BOARD
              if (cpt == 2) { // VID
                if (curr < (FEC_K+1)) {
                  memset(&buf_vid[curr][0],0,ONLINE_MTU);
    	          piov = &iov[curr];
                  piov->iov_base = &buf_vid[curr][sizeof(wfb_utils_fec_t)];
                  piov->iov_len = PAY_MTU;
                  piov->iov_len = readv( fd[cpt], piov, 1);
                  piov->iov_base = &buf_vid[curr][0];
                  piov->iov_len += sizeof(wfb_utils_fec_t);
                  ((wfb_utils_fec_t *)piov->iov_base)->feclen = piov->iov_len;
      	          curr++;
    	    
    	          printf("len(%ld)  ",piov->iov_len - sizeof(wfb_utils_fec_t));
    	          for (uint8_t i=0;i<5;i++) printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf(" ... ");
    	          for (uint16_t i=piov->iov_len-5-sizeof(wfb_utils_fec_t);i<piov->iov_len-sizeof(wfb_utils_fec_t);i++) 
    	          printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf("\n");
    	        }
	      }
#endif // BOARD
	    }
	  }
	}
      }
#if BOARD
      if (curr == FEC_K) {
        printf("\n");

        curr=0;
        unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
        uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)&buf_vid[f];
        uint8_t *fecblocks[FEC_N-FEC_K];
        for (uint8_t f=0; f<(FEC_N - FEC_K); f++) {
          fecblocks[f] = (uint8_t *)&buf_vid[f + FEC_K];
        }
        fec_encode(fec_p,
                    (const gf*restrict const*restrict const)datablocks,
                    (gf*restrict const*restrict const)fecblocks,
                    (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);

        for (uint8_t k=0;k<FEC_N;k++) {
          iovfec[k] = &iov[k];
          if (k>=FEC_K) { iov[k].iov_base = &buf_vid[k][0]; iov[k].iov_len = ONLINE_MTU; }
	}



	printf("---------------------------------------------------------------------\n");
      }
#endif // BOARD
    }
  }
}
