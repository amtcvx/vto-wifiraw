#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#include "wfb.h"
#include "wfb_utils.h"

/*****************************************************************************/
int main(void) {
  uint64_t exptime;
  ssize_t len;
  uint8_t num=0, seq=0;

  wfb_utils_init_t utils;
  wfb_utils_init(&utils);

  for(;;) {     
    if (0 != poll(utils.readsets, utils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<utils.nbdev; cpt++) {
        if (utils.readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(utils.fd[cpt], &exptime, sizeof(uint64_t));
            wfb_utils_periodic(&utils);
          } else if ((cpt > 0)&&(cpt <= utils.nbraws)) {
            wfb_utils_heads_pay_t headspay;
	    memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
	    memset(utils.raws.headsrx->llchd_rx, 0, sizeof(utils.raws.headsrx->llchd_rx));
	    struct iovec iov1 = { .iov_base = utils.raws.headsrx->radiotaphd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->radiotaphd_rx)};
	    struct iovec iov2 = { .iov_base = utils.raws.headsrx->ieeehd_rx,
                                    .iov_len = sizeof(utils.raws.headsrx->ieeehd_rx)};
	    struct iovec iov3 = { .iov_base = utils.raws.headsrx->llchd_rx,
                                    .iov_len = sizeof(utils.raws.headsrx->llchd_rx)};
            struct iovec iov4 = { .iov_base = &headspay,
                                    .iov_len = sizeof(wfb_utils_heads_pay_t)};
            struct iovec iov5 = utils.msgin.eltin[cpt-1].iov[ utils.msgin.eltin[cpt-1].curr ];
            struct iovec iovtab[5] = {iov1, iov2, iov3, iov4, iov5};
	    struct msghdr msg;
            msg.msg_iov = iovtab;
            msg.msg_iovlen = 5;
	    len = recvmsg(utils.fd[cpt], &msg, MSG_DONTWAIT);
            if (!((len > 0) && 
#if BOARD
            (headspay.droneid == DRONEID_GRD)
#else
            (headspay.droneid >= DRONEID_MIN)&&(headspay.droneid <= DRONEID_MAX)
#endif
             &&(((uint8_t *)iov3.iov_base)[0]==1)&&(((uint8_t *)iov3.iov_base)[1]==2)
	     &&(((uint8_t *)iov3.iov_base)[2]==3)&&(((uint8_t *)iov3.iov_base)[3]==4))) {
	      utils.rawdevs[cpt-1]->stat.fails++;
	    } else if( headspay.msgcpt == WFB_PRO) {
              utils.rawdevs[cpt-1]->stat.incoming++;
	      utils.rawdevs[cpt-1]->stat.chan = ((wfb_utils_pro_t *)iov5.iov_base)->chan;
              printf("IN (%d)  (%d)\n",cpt-1,((wfb_utils_pro_t *)iov5.iov_base)->chan);
	    } else if( headspay.msgcpt == WFB_TUN) {
	      if ((len = write(utils.fd[utils.nbraws + 1], iov5.iov_base, headspay.msglen)) > 0) printf("TUN write(%ld)\n",len);
	    } else if( headspay.msgcpt == WFB_VID) {
              //utils.msgin.eltin[cpt-1].iov[headspay.fec] = utils.msgin.eltin[cpt-1].iov[ utils.msgin.eltin[cpt-1].curr ] ;
              //utils.msgin.eltin[cpt-1].curr++;
	      if (headspay.fec < FEC_K) {
                if ((len = sendto(utils.fd[utils.nbraws + 3], iov5.iov_base, headspay.msglen, MSG_DONTWAIT, 
		      (struct sockaddr *)&(utils.vidout), sizeof(struct sockaddr))) > 0) {
                  printf("VID write(%d)(%ld)\n",headspay.fec,len);
		}
	      } 

              //wfb_utils_displayvid(&utils);
	    }
	    wfb_net_drain(utils.fd[cpt]);
          } else if (cpt == utils.nbraws + 1) { // WFB_TUN
	    struct iovec *piov = &utils.msgout.iov[WFB_TUN][0][0];
            piov->iov_len = ONLINE_MTU;
            piov->iov_len = readv( utils.fd[cpt], piov, 1);
            if (utils.rawchan.mainraw == -1) piov->iov_len = 0;
	    printf("TUN readv(%ld)\n",piov->iov_len);
          } else if (cpt == utils.nbraws + 3) { // WFB_VID
	    uint8_t curr = 0;
            if (utils.rawchan.mainraw != -1) curr = utils.msgout.currvid;
	    struct iovec *piov = &utils.msgout.iov[WFB_VID][0][curr];
            piov->iov_len = ONLINE_MTU;
	    memset(piov->iov_base, 0, piov->iov_len);
            piov->iov_len = readv( utils.fd[cpt], piov, 1);
            if (utils.rawchan.mainraw == -1) piov->iov_len = 0;
	    else if (curr < FEC_K) (utils.msgout.currvid)++;
	    printf("VID readv(%ld)\n",piov->iov_len);
	  }
        }
      }

      for (uint8_t i=0;i<WFB_NB;i++) {
        uint8_t jmax = 0, kmax = 0;
        if (i == WFB_PRO) jmax = utils.nbraws;
        if ((i == WFB_VID) && (utils.msgout.currvid == FEC_K)) {

	  kmax = (FEC_N  - 1);
          unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
	  uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)&utils.msgout.buf_vid[f];
	  uint8_t *fecblocks[FEC_N-FEC_K]; 
	  for (uint8_t f=0; f<(FEC_N - FEC_K); f++) {
	    fecblocks[f] = (uint8_t *)&utils.msgout.buf_vid[f + FEC_K];
            utils.msgout.iov[WFB_VID][0][f + FEC_K].iov_len = ONLINE_MTU;
	  }
	  fec_encode(utils.fec_p,
			 (const gf*restrict const*restrict const)datablocks,
			 (gf*restrict const*restrict const)fecblocks,
			 (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);
	  printf("ENCODED\n");
	}

        for (uint8_t j=0;j<=jmax;j++) {
          for (uint8_t k=0;k<=kmax;k++) {
    	    struct iovec iov5 = utils.msgout.iov[i][j][k];
            if ((iov5.iov_len > 0)  && (((i == WFB_VID) && (utils.msgout.currvid == FEC_K)) || (i != WFB_VID))) {

              wfb_utils_heads_pay_t headspay = 
  	          { .droneid = DRONEID, .msgcpt = i, .msglen = iov5.iov_len,.seq = seq, .fec = k, .num = num++ };
      	      struct iovec iov1 = { .iov_base = utils.raws.headstx->radiotaphd_tx,
                                            .iov_len = utils.raws.headstx->radiotaphd_tx_size};
      	      struct iovec iov2 = { .iov_base = utils.raws.headstx->ieeehd_tx,
                                            .iov_len = utils.raws.headstx->ieeehd_tx_size};
      	      struct iovec iov3 = { .iov_base = utils.raws.headstx->llchd_tx,
                                            .iov_len = utils.raws.headstx->llchd_tx_size};
      	      struct iovec iov4 = { .iov_base = &headspay,
                                            .iov_len = sizeof(wfb_utils_heads_pay_t)};
      	      struct iovec iovtab[5] = {iov1, iov2, iov3, iov4, iov5};
      	      struct msghdr msg;
      	      msg.msg_iov = iovtab;
              msg.msg_iovlen = 5;
              printf("OUT (%d)(%d)  (%ld)\n",i,j,iov5.iov_len);
    	      if (i == WFB_PRO) printf("Chan =%d\n",((wfb_utils_pro_t *)iov5.iov_base)->chan);
    	      if (i == WFB_VID) printf("fec%d\n",headspay.fec);
      	      len = sendmsg(utils.fd[1 + j], (const struct msghdr *)&msg, MSG_DONTWAIT);
      	      if (len > 0) utils.rawdevs[j]->stat.sent++;
    	      if ((i == WFB_VID) && (k == (FEC_N - 1))) utils.msgout.currvid = 0;
    	      utils.msgout.iov[i][j][k].iov_len = 0;
	    }
  	  }
        }
      }
    }
  }
  return(0);
}
