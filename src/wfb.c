#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>

#include "wfb.h"
#include "wfb_utils.h"


/*****************************************************************************/
int main(void) {
  uint64_t exptime;
  ssize_t len;
  uint8_t num=0, seq=0;

  wfb_utils_init_t utils;
  wfb_utils_init(&utils);
  
  printf("(%d)\n",utils.nbraws);

  for(;;) {     
    if (0 != poll(utils.readsets, utils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<utils.nbdev; cpt++) {
        if (utils.readsets[cpt].revents == POLLIN) {
          if (cpt == 0) {
            len = read(utils.fd[cpt], &exptime, sizeof(uint64_t));
            wfb_utils_periodic(&utils);
          } else {
            if ((cpt > 0)&&(cpt <= utils.nbraws)) {
              printf("RAW (%d)\n",cpt);
              wfb_utils_presetrawmsg(&(utils.raws), true);
              len = recvmsg( utils.fd[cpt], &utils.raws.rawmsg[utils.raws.rawmsgcurr].msg, MSG_DONTWAIT);
              if (!((len > 0)&&(utils.raws.pay.droneid >= DRONEIDMIN)&&(utils.raws.pay.droneid <= DRONEIDMAX))) utils.rawdevs[cpt-1]->stat.fails++;
              else { 
                utils.rawdevs[cpt-1]->stat.incoming++;
		wfb_utils_down_t *pay = utils.raws.rawmsg[utils.raws.rawmsgcurr].headvecs.head[wfb_utils_datapos].iov_base;
		utils.rawdevs[cpt-1]->stat.chan = pay->chan;
              }
            }
          }
        }
      }

      for (uint8_t i=0;i<utils.nbraws;i++) {
        for (uint8_t j=0;j< MAXMSG;j++) {
          for (uint8_t k=0;k<FEC_N;k++) {
            if (utils.downmsg.iov[i][j][k].iov_len != 0) {
              wfb_utils_presetrawmsg(&(utils.raws), false);

	      wfb_utils_pay_t *pay = utils.raws.rawmsg[utils.raws.rawmsgcurr].msg.msg_iov->iov_base;
              pay->msgcpt = j;
              pay->droneid = DRONEID;
              pay->seq = seq;
              pay->fec = k;
              pay->num = num++;
              pay->msglen = utils.downmsg.iov[i][j][k].iov_len ;

      	      utils.raws.rawmsg[i].headvecs.head[wfb_utils_datapos] = utils.downmsg.iov[i][j][k];

              len = sendmsg(utils.fd[1 + i], &utils.raws.rawmsg[utils.raws.rawmsgcurr].msg, MSG_DONTWAIT);
	      if (len > 0) utils.rawdevs[i]->stat.sent++;
	      utils.downmsg.iov[i][j][k].iov_len = 0;
	    }
	  }
	}
      }

    }
  }
  return(0);
}
