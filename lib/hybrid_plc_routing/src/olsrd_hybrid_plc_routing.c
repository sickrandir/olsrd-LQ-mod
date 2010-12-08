#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <faifa.h>
#include <frame.h>
#include <net/ethernet.h>
#include <homeplug.h>
#include <homeplug_av.h>
#include <endian.h>


#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_arp.h>
#ifndef android
#include <net/ethernet.h>
#endif
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netpacket/packet.h>
#include <linux/types.h>
#include <linux/filter.h>
#include <unistd.h>

#include "olsrd_hybrid_plc_routing.h"
#include "kernel_routes.h"
#include "scheduler.h"

#define PLUGIN_INTERFACE_VERSION 5

/****************************************************************************
 *                Functions that the plugin MUST provide                    *
 ****************************************************************************/

/**
 * Plugin interface version
 * Used by main olsrd to check plugin interface version
 */
int
olsrd_plugin_interface_version(void)
{
  return PLUGIN_INTERFACE_VERSION;
}

static const struct olsrd_plugin_parameters plugin_parameters[] = {
};

void
olsrd_get_plugin_parameters(const struct olsrd_plugin_parameters **params, int *size)
{
  *params = plugin_parameters;
  *size = sizeof(plugin_parameters) / sizeof(*plugin_parameters);
}

typedef struct {
  struct ethhdr eth;
  struct iphdr ip;
  struct udphdr udp;
} __attribute__ ((packed)) arprefresh_buf;

static int arprefresh_sockfd = -1;
static const int arprefresh_portnum = 698;





