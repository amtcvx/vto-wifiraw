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
            msg_eltin_t *pelt = &utils.msgin.eltin[cpt-1];
	    pelt->iovraw[pelt->curr].iov_len = ONLINE_MTU;
	    struct iovec *piovpay = &pelt->iovraw[pelt->curr];
#if RAW
	    memset(utils.raws.headsrx->llchd_rx, 0, sizeof(utils.raws.headsrx->llchd_rx));
	    struct iovec iov1 = { .iov_base = utils.raws.headsrx->radiotaphd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->radiotaphd_rx)};
	    struct iovec iov2 = { .iov_base = utils.raws.headsrx->ieeehd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->ieeehd_rx)};
	    struct iovec iov3 = { .iov_base = utils.raws.headsrx->llchd_rx,
                                  .iov_len = sizeof(utils.raws.headsrx->llchd_rx)};
            struct iovec iovtab[5] = {iov1, iov2, iov3, iovheadpay, *piovpay};
	    msg.msg_iovlen = 5;
#else // RAW
            struct iovec iovtab[2] = {iovheadpay, *piovpay};
            msg.msg_iovlen = 2;
#endif // RAW
            msg.msg_iov = iovtab;
	    len = recvmsg(utils.fd[cpt], &msg, MSG_DONTWAIT);
	    piovpay->iov_len = headspay.msglen;
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
	        utils.rawdevs[cpt-1]->stat.chan = ((wfb_utils_pro_t *)piovpay->iov_base)->chan;
	      }
#else // RAW
            if (len > 0) {
#endif // RAW
	      if( headspay.msgcpt == WFB_TUN) {
	        len = write(utils.fd[utils.nbraws + 1], piovpay->iov_base, piovpay->iov_len);
	      } 
              if( headspay.msgcpt == WFB_VID) {
                bool clearflag=false;


                if ((headspay.seq == 1) && (headspay.fec == 2)) {
		  printf("\nMISSING (%d)(%d)\n",headspay.seq,headspay.fec);
                  printf("(%d)[%d]len(%ld)  ",pelt->curr,headspay.fec,piovpay->iov_len);
	          for (uint8_t i=0;i<5;i++) printf("%x ",*((uint8_t *)(pelt->iovraw[pelt->curr].iov_base + i)));printf(" ... ");
	          for (uint16_t i=piovpay->iov_len-5;i<piovpay->iov_len;i++) printf("%x ",*((uint8_t *)(pelt->iovraw[pelt->curr].iov_base + i)));;printf("\n");
		  printf("\n");
		  break;
                }


		uint8_t imax=0, imin=0;
                if ((pelt->nxtseq != headspay.seq)||(pelt->nxtfec != headspay.fec)) {
		  if (headspay.fec < (FEC_N-1)) { pelt->nxtfec=(headspay.fec+1); pelt->nxtseq=headspay.seq; }
		  else { 
		    pelt->nxtfec=0;
		    if (headspay.seq < 254) pelt->nxtseq=(headspay.seq+1); else pelt->nxtseq = 0;
		  }
//		  printf("KO\n");
		  pelt->fails = true;
		} else {
//	          printf("OK\n");
		  if (pelt->nxtfec < (FEC_N-1)) (pelt->nxtfec)++; 
		  else { pelt->nxtfec=0; if (pelt->nxtseq < 255) (pelt->nxtseq)++; else pelt->nxtseq = 0; }
  	          if (headspay.fec < FEC_K) {imin=headspay.fec; imax=(1+imin); }
		}
              
		if (pelt->curseq == headspay.seq) pelt->iovfec[headspay.fec] = piovpay; else { pelt->iovsto = piovpay; pelt->fecsto = headspay.fec; }
		if (pelt->curr < (MAXNBMTUIN-1)) pelt->curr=(1+pelt->curr); else pelt->curr=0;

                if (pelt->curseq != headspay.seq) {
                  pelt->curseq = headspay.seq;

		  if (pelt->fails) {
                    pelt->fails = false;

                    printf("Inputs ...\n");
		    for (uint8_t k=0;k<FEC_N;k++) {
                      if (pelt->iovfec[k]) {
                        struct iovec *ptmp = pelt->iovfec[k];
                        printf("[%d] len(%ld)  ",k,ptmp->iov_len);
	                for (uint8_t i=0;i<5;i++) printf("%x ",*(((uint8_t *)ptmp->iov_base)+i));printf(" ... ");
	                for (uint16_t i=ptmp->iov_len-5;i<ptmp->iov_len;i++) printf("%x ",*(((uint8_t *)ptmp->iov_base)+i));printf("\n");
		      }
		    }

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
                      if ( pelt->iovfec[k] ) {
                        inblocks[k] = (uint8_t *)pelt->iovfec[k]->iov_base;
                        index[k] = k;
                        alldata |= (1 << k);
                      } else {
                        for(;j < FEC_N; j++) {
                          if ( pelt->iovfec[j] ) {
                            inblocks[k] = (uint8_t *)pelt->iovfec[j]->iov_base;
                            outblocks[idx] = &outblocksbuf[idx][0]; idx++;
                            index[k] = j;
                            j++;
        	            alldata |= (1 << k);
        	            break;
        	          }
  		        }
  		      }
		    }
		    if ((alldata == 255)&&(idx > 0)&&(idx <= (FEC_N - FEC_K))) {
                      for (uint8_t k=0;k<FEC_K;k++) printf("%d ",index[k]);
                      printf("\nDECODE (%d)\n",idx);
                      fec_decode(utils.fec_p, 
                               (const unsigned char **)inblocks,
                               (unsigned char * const*)outblocks,
                               (unsigned int *)index,
                               ONLINE_MTU);

  		    }
		  }
		  clearflag = true;
		}
/*
		for (uint8_t i=imin;i<imax;i++) 
                  if ((len = sendto(utils.fd[utils.nbraws + 3], pelt->iovfec[i].iov_base + sizeof(wfb_utils_fec_t), 
				    pelt->iovfec[i].iov_len - sizeof(wfb_utils_fec_t), MSG_DONTWAIT, 
  	                            (struct sockaddr *)&(utils.vidout), sizeof(struct sockaddr))) > 0) printf("len(%ld)\n",len);

		imax=0; imin=0;
*/
		if (clearflag) {
		  clearflag=false;
		  for (uint8_t k=0;k<FEC_N;k++) { 
                    if(pelt->iovsto) { pelt->iovfec[headspay.fec]=pelt->iovsto; pelt->iovsto=(struct iovec *)0; pelt->iovsto=0; }
                    else pelt->iovfec[k]=(struct iovec *)0;
		  }
		}
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
            memset(&utils.msgout.buf_vid[curr][0],0,ONLINE_MTU);
            piov->iov_base = &utils.msgout.buf_vid[curr][sizeof(wfb_utils_fec_t)];
	    piov->iov_len = PAY_MTU;
            piov->iov_len = readv( utils.fd[cpt], piov, 1);
            piov->iov_base = &utils.msgout.buf_vid[curr][0];
	    piov->iov_len += sizeof(wfb_utils_fec_t);
	    ((wfb_utils_fec_t *)piov->iov_base)->feclen = piov->iov_len;
/*
            printf("len(%ld)  ",piov->iov_len);
	    for (uint8_t i=0;i<5;i++) printf("%x ",*(((uint8_t *)piov->iov_base)+i));printf(" ... ");
	    for (uint16_t i=piov->iov_len-5;i<piov->iov_len;i++) printf("%x ",*(((uint8_t *)piov->iov_base)+i));printf("\n");
*/
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
    	    struct iovec *piovpay = &utils.msgout.iov[i][j][k];
            if ((piovpay->iov_len > 0)  && (((i == WFB_VID) && (utils.msgout.currvid == FEC_K)) || (i != WFB_VID))) {

              wfb_utils_heads_pay_t headspay = 
  	          { .droneid = DRONEID, .msgcpt = i, .msglen = piovpay->iov_len, .seq = utils.msgout.eltout[j].seq, 
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
              struct iovec iovtab[5] = {iov1, iov2, iov3, iovheadpay, *piovpay};
	      msg.msg_iovlen = 5;
#else // RAW
              struct iovec iovtab[2] = {iovheadpay, *piovpay};
              msg.msg_iovlen = 2;
#endif // RAW
      	      msg.msg_iov = iovtab;
#if RAW
#else
      	      msg.msg_name = &utils.norawout;
      	      msg.msg_namelen = sizeof(utils.norawout);
#endif // RAW
      	      len = sendmsg(utils.fd[1 + j], (const struct msghdr *)&msg, MSG_DONTWAIT);

              if (i == WFB_VID) {
                printf("[%d] len(%ld)  ",k,piovpay->iov_len);
	        for (uint8_t i=0;i<5;i++) printf("%x ",*(((uint8_t *)piovpay->iov_base)+i));printf(" ... ");
	        for (uint16_t i=piovpay->iov_len-5;i<piovpay->iov_len;i++) printf("%x ",*(((uint8_t *)piovpay->iov_base)+i));printf("\n");
	      }

#if RAW
      	      if (len > 0) utils.rawdevs[j]->stat.sent++;
#endif // RAW
    	      utils.msgout.iov[i][j][k].iov_len = 0;
    	      if ((i == WFB_VID) && (k == (FEC_N - 1))) { utils.msgout.currvid = 0; utils.msgout.eltout[j].seq++;printf("\n");};
	    }
  	  }
        }
      }
    }
  }
  return(0);
}
