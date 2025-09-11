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
            struct msghdr msg;
            wfb_utils_heads_pay_t headspay;
	    memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
            struct iovec iovheadpay = { .iov_base = &headspay,
                                        .iov_len = sizeof(wfb_utils_heads_pay_t)};
            struct iovec iovpay = utils.msgin.eltin[cpt-1].iov[ utils.msgin.eltin[cpt-1].curr ];
#if RAW
	    memset(utils.raws.headsrx->llchd_rx, 0, sizeof(utils.raws.headsrx->llchd_rx));
	    struct iovec iov1 = { .iov_base = utils.raws.headsrx->radiotaphd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->radiotaphd_rx)};
	    struct iovec iov2 = { .iov_base = utils.raws.headsrx->ieeehd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->ieeehd_rx)};
	    struct iovec iov3 = { .iov_base = utils.raws.headsrx->llchd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->llchd_rx)};
            struct iovec iovtab[5] = {iov1, iov2, iov3, iovheadpay, iovpay};
	    msg.msg_iovlen = 5;
#else // RAW
            struct iovec iovtab[2] = {iovheadpay, iovpay};
            msg.msg_iovlen = 2;
#endif // RAW
            msg.msg_iov = iovtab;
	    len = recvmsg(utils.fd[cpt], &msg, MSG_DONTWAIT);
#if RAW
            if (!((len > 0) && 
#if BOARD
              (headspay.droneid == DRONEID_GRD)
#else // BOARD
              (headspay.droneid >= DRONEID_MIN)&&(headspay.droneid <= DRONEID_MAX)
#endif // BOARD
              &&(((uint8_t *)iov3.iov_base)[0]==1)&&(((uint8_t *)iov3.iov_base)[1]==2)
	      &&(((uint8_t *)iov3.iov_base)[2]==3)&&(((uint8_t *)iov3.iov_base)[3]==4))) {
	      utils.rawdevs[cpt-1]->stat.fails++;
	    } else {
	      if( headspay.msgcpt == WFB_PRO) {
                utils.rawdevs[cpt-1]->stat.incoming++;
	        utils.rawdevs[cpt-1]->stat.chan = ((wfb_utils_pro_t *)iovpay.iov_base)->chan;
	      }
#else // RAW
            if (len > 0) {
#endif // RAW
	      if( headspay.msgcpt == WFB_TUN) {
	        len = write(utils.fd[utils.nbraws + 1], iovpay.iov_base, headspay.msglen);
	      } 
              if( headspay.msgcpt == WFB_VID) {
                //utils.msgin.eltin[cpt-1].iov[headspay.fec] = utils.msgin.eltin[cpt-1].iov[ utils.msgin.eltin[cpt-1].curr ] ;
                //utils.msgin.eltin[cpt-1].curr++;
		
		//if (utils.msgin.eltin[cpt-1].seq != headspay.seq) {
		//  utils.msgin.eltin[cpt-1].seq = headspay.seq;
		//  for (uint8_t k=0; k < headspay.fec; k++) utils.msgin.eltin[cpt-1].fec[k] = utils.msgin.eltin[cpt-1].storefec;
		//  utils.msgin.eltin[cpt-1].fec[headspay.fec] = utils.msgin.eltin[cpt-1].curr;

		msg_eltin_t *ptrelt = &utils.msgin.eltin[cpt-1];
                if ((ptrelt->nxtseq == headspay.seq)&&(ptrelt->nxtfec == headspay.fec)) {

  	          if (headspay.fec < FEC_K) {
                    if ((len = sendto(utils.fd[utils.nbraws + 3], iovpay.iov_base, headspay.msglen, MSG_DONTWAIT, 
  	                      (struct sockaddr *)&(utils.vidout), sizeof(struct sockaddr))) > 0) {
                      printf("len(%ld)\n",len);
  		    }
		  }
		  if (ptrelt->nxtfec < (FEC_N-1)) (ptrelt->nxtfec)++; else { ptrelt->nxtfec=0; if (ptrelt->nxtseq < 255) (ptrelt->nxtseq)++; else ptrelt->nxtseq = 0; }
		} else {
                  printf("IN KO (%d)(%d)  (%d)(%d)\n",headspay.fec, ptrelt->nxtfec, headspay.seq, ptrelt->nxtseq);
		  if (headspay.fec < (FEC_N-1)) { ptrelt->nxtfec=(headspay.fec+1); ptrelt->nxtseq=headspay.seq; }
		  else { 
		    ptrelt->nxtfec=0;
		    if (headspay.seq < 254) ptrelt->nxtseq=(headspay.seq+1); else ptrelt->nxtseq = 0;
		  }
                  printf("OUT KO (%d)(%d)  (%d)(%d)\n",headspay.fec, ptrelt->nxtfec, headspay.seq, ptrelt->nxtseq);
		}	

/*
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
                    if ( utils.msgin.eltin[cpt-1].iov[k].iov_len != 0 ) {
                      inblocks[k] = (uint8_t *)utils.msgin.eltin[cpt-1].iov[k].iov_base;
                      index[k] = k;
                      alldata |= (1 << k);
                    } else {
                      for(;j < FEC_N; j++) {
                        if ( utils.msgin.eltin[cpt-1].iov[j].iov_len !=0 ) {
                          inblocks[k] = (uint8_t *)utils.msgin.eltin[cpt-1].iov[j].iov_base;
                          outblocks[idx] = &outblocksbuf[idx][0]; idx++;
                          index[k] = j;
                          j++;
      	                  alldata |= (1 << k);
      	                  break;
      	                }
		      }
		    }
		  }
      	          if (alldata == 255) {
                    fec_decode(utils.fec_p, 
                             (const unsigned char **)inblocks,
                             (unsigned char * const*)outblocks,
                             (unsigned int *)index,
                             ONLINE_MTU);
		  }
*/
	      } 
	    }
#if RAW
	    wfb_net_drain(utils.fd[cpt]);
#endif // RAW

/*****************************************************************************/       
          } else if (cpt == utils.nbraws + 1) { // WFB_TUN
	    struct iovec *piov = &utils.msgout.iov[WFB_TUN][0][0];
            piov->iov_len = ONLINE_MTU;
            piov->iov_len = readv( utils.fd[cpt], piov, 1);
            if (utils.rawchan.mainraw == -1) piov->iov_len = 0;
          } else if (cpt == utils.nbraws + 3) { // WFB_VID
	    uint8_t curr = 0;
            if (utils.rawchan.mainraw != -1) curr = utils.msgout.currvid;
	    struct iovec *piov = &utils.msgout.iov[WFB_VID][0][curr];
            piov->iov_len = ONLINE_MTU;
	    memset(piov->iov_base, 0, piov->iov_len);
            piov->iov_len = readv( utils.fd[cpt], piov, 1);
            if (utils.rawchan.mainraw == -1) piov->iov_len = 0;
	    else if (curr < FEC_K) (utils.msgout.currvid)++;
	  }
        }
      }

/*****************************************************************************/
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
	}

        for (uint8_t j=0;j<=jmax;j++) {
          for (uint8_t k=0;k<=kmax;k++) {
    	    struct iovec iovpay = utils.msgout.iov[i][j][k];
            if ((iovpay.iov_len > 0)  && (((i == WFB_VID) && (utils.msgout.currvid == FEC_K)) || (i != WFB_VID))) {

              wfb_utils_heads_pay_t headspay = 
  	          { .droneid = DRONEID, .msgcpt = i, .msglen = iovpay.iov_len, .seq = utils.msgout.eltout[j].seq, 
		    .fec = k, .num = (utils.msgout.eltout[j].num)++ };

              struct iovec iovheadpay = { .iov_base = &headspay,
                                          .iov_len = sizeof(wfb_utils_heads_pay_t)};
              struct msghdr msg;
#if RAW
      	      struct iovec iov1 = { .iov_base = utils.raws.headstx->radiotaphd_tx,
                                    .iov_len = utils.raws.headstx->radiotaphd_tx_size};
      	      struct iovec iov2 = { .iov_base = utils.raws.headstx->ieeehd_tx,
                                    .iov_len = utils.raws.headstx->ieeehd_tx_size};
      	      struct iovec iov3 = { .iov_base = utils.raws.headstx->llchd_tx,
                                    .iov_len = utils.raws.headstx->llchd_tx_size};
              struct iovec iovtab[5] = {iov1, iov2, iov3, iovheadpay, iovpay};
	      msg.msg_iovlen = 5;
#else // RAW
              struct iovec iovtab[2] = {iovheadpay, iovpay};
              msg.msg_iovlen = 2;
#endif // RAW
      	      msg.msg_iov = iovtab;
#if RAW
#else
      	      msg.msg_name = &utils.norawout;
      	      msg.msg_namelen = sizeof(utils.norawout);
#endif // RAW
      	      len = sendmsg(utils.fd[1 + j], (const struct msghdr *)&msg, MSG_DONTWAIT);
#if RAW
      	      if (len > 0) utils.rawdevs[j]->stat.sent++;
#endif // RAW
    	      utils.msgout.iov[i][j][k].iov_len = 0;
    	      if ((i == WFB_VID) && (k == (FEC_N - 1))) { utils.msgout.currvid = 0; utils.msgout.eltout[j].seq++;};
	    }
  	  }
        }
      }
    }
  }
  return(0);
}
