#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>

#include "wfb.h"
#include "wfb_utils.h"


/*****************************************************************************/
int main(void) {
  uint8_t devcpt;
  uint64_t exptime;
  ssize_t len;

  wfb_utils_init_t utils;
  wfb_utils_init(&utils);

  printf("(%d)\n",utils.rawlimit-1);

  for(;;) {	
    if (0 != poll(utils.readsets, utils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<utils.nbdev; cpt++) {
        if (utils.readsets[cpt].revents == POLLIN) {
          devcpt = utils.readtab[cpt];
          if (devcpt == 0) {
            len = read(utils.fd[devcpt], &exptime, sizeof(uint64_t));
	    wfb_utils_periodic(&(utils.stat));
	  } else {
            if ((devcpt > 0)&&(devcpt < utils.rawlimit)) {
  	      printf("RAW (%d)\n",devcpt);
              wfb_utils_presetrawmsg(&(utils.raws), true);
              len = recvmsg( utils.fd[devcpt], &utils.raws.rawmsg[utils.raws.rawmsgcurr].msg, MSG_DONTWAIT);
	      if (!((len > 0)&&(utils.raws.pay.droneid >= DRONEIDMIN)&&(utils.raws.pay.droneid <= DRONEIDMAX))) printf("putils.stat.raws[devcpt-1].fails++\n");
	      else {
                printf("piutils.pnet->raws[devcpt-1].incoming++\n");
	      }
	    }
          }
	}
      }
    }
  }
  return(0);
}
