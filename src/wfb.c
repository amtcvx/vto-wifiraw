#include <stdio.h>
#include <stdlib.h>
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

  for (uint8_t cpt=0; cpt<utils.nbraws; cpt++)
    if (!(wfb_net_setfreq(utils.sockidnl, utils.rawdevs[cpt].ifindex, utils.rawdevs[cpt].freqs[cpt]))) exit(-1);

  for(;;) {	
    if (0 != poll(utils.readsets, utils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<utils.nbdev; cpt++) {
        if (utils.readsets[cpt].revents == POLLIN) {
          devcpt = utils.readtab[cpt];
          if (devcpt == 0) {
            len = read(utils.fd[devcpt], &exptime, sizeof(uint64_t));
	    wfb_utils_periodic(&utils);
	  } else {
            if ((devcpt > 0)&&(devcpt < utils.rawlimit)) {
  	      printf("RAW (%d)\n",devcpt);
              wfb_utils_presetrawmsg(&(utils.raws), true);
              len = recvmsg( utils.fd[devcpt], &utils.raws.rawmsg[utils.raws.rawmsgcurr].msg, MSG_DONTWAIT);
	      if (!((len > 0)&&(utils.raws.pay.droneid >= DRONEIDMIN)&&(utils.raws.pay.droneid <= DRONEIDMAX))) utils.rawdevs[devcpt-1].fails++;
	      else { 
                utils.rawdevs[devcpt-1].incoming++;
	      }
	    }
          }
	}
      }
    }
  }
  return(0);
}
