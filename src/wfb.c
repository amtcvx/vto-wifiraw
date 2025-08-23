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
              }
            }
          }
        }
      }
      for (uint8_t cpt=0; cpt<utils.nbraws; cpt++) {
        wfb_utils_presetrawmsg(&(utils.raws), false);
        len = sendmsg(utils.fd[1 + cpt], &utils.raws.rawmsg[utils.raws.rawmsgcurr].msg, MSG_DONTWAIT);
	if (len > 0) utils.rawdevs[cpt]->stat.sent++;
      }
    }
  }
  return(0);
}
