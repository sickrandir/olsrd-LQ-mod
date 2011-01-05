
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2008 Henning Rogge <rogge@fgan.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#include "tc_set.h"
#include "link_set.h"
#include "lq_plugin.h"
#include "olsr_spf.h"
#include "lq_packet.h"
#include "packet.h"
#include "olsr.h"
#include "lq_plugin_default_hybrid_plc.h"
#include "parser.h"
#include "fpm.h"
#include "mid_set.h"
#include "scheduler.h"
#include "log.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <ctype.h>


#define LQ_PLUGIN_LC_MULTIPLIER 1024
#define LQ_PLUGIN_RELEVANT_COSTCHANGE_FF 16

static void default_lq_initialize_hybrid_plc(void);

static olsr_linkcost default_lq_calc_cost_hybrid_plc(const void *lq);

static void default_lq_packet_loss_worker_hybrid_plc(struct link_entry *link, void *lq, bool lost);
static void default_lq_memorize_foreign_hello_hybrid_plc(void *local, void *foreign);

static int default_lq_serialize_hello_lq_pair_hybrid_plc(unsigned char *buff, void *lq);
static void default_lq_deserialize_hello_lq_pair_hybrid_plc(const uint8_t ** curr, void *lq);
static int default_lq_serialize_tc_lq_pair_hybrid_plc(unsigned char *buff, void *lq);
static void default_lq_deserialize_tc_lq_pair_hybrid_plc(const uint8_t ** curr, void *lq);

static void default_lq_copy_link2neigh_hybrid_plc(void *t, void *s);
static void default_lq_copy_link2tc_hybrid_plc(void *target, void *source);
static void default_lq_clear_hybrid_plc(void *target);
static void default_lq_clear_hybrid_plc_hello(void *target);

static const char *default_lq_print_hybrid_plc(void *ptr, char separator, struct lqtextbuffer *buffer);
static const char *default_lq_print_cost_hybrid_plc(olsr_linkcost cost, struct lqtextbuffer *buffer);

static void update_plc_data(void);
static void deserialize_stations_data(unsigned char *buff);

struct plc_data {
	u_int8_t mac[6];
	u_int8_t lq;
	u_int8_t nlq;
};
u_int8_t plc_mac[6];
struct plc_data *p_data;
u_int8_t p_size_t;
int socket_fd;
struct sockaddr_un name;


/* etx lq plugin (freifunk fpm version) settings */
struct lq_handler lq_etx_hybrid_plc_handler = {
  &default_lq_initialize_hybrid_plc,
  &default_lq_calc_cost_hybrid_plc,
  &default_lq_calc_cost_hybrid_plc,

  &default_lq_packet_loss_worker_hybrid_plc,

  &default_lq_memorize_foreign_hello_hybrid_plc,
  &default_lq_copy_link2neigh_hybrid_plc,
  &default_lq_copy_link2tc_hybrid_plc,
  &default_lq_clear_hybrid_plc_hello,
  &default_lq_clear_hybrid_plc,

  &default_lq_serialize_hello_lq_pair_hybrid_plc,
  &default_lq_serialize_tc_lq_pair_hybrid_plc,
  &default_lq_deserialize_hello_lq_pair_hybrid_plc,
  &default_lq_deserialize_tc_lq_pair_hybrid_plc,

  &default_lq_print_hybrid_plc,
  &default_lq_print_hybrid_plc,
  &default_lq_print_cost_hybrid_plc,

  sizeof(struct default_lq_hybrid_plc_hello),
  sizeof(struct default_lq_hybrid_plc),
  4,
  4
};

static void
default_lq_hybrid_plc_handle_lqchange(void) {
  struct default_lq_hybrid_plc_hello *lq;
  struct ipaddr_str buf;
  struct link_entry *link;

  bool triggered = false;

  OLSR_FOR_ALL_LINK_ENTRIES(link) {
    bool relevant = false;
    lq = (struct default_lq_hybrid_plc_hello *)link->linkquality;

#if 0
  fprintf(stderr, "%s: old = %u/%u   new = %u/%u\n", olsr_ip_to_string(&buf, &link->neighbor_iface_addr),
      lq->smoothed_lq.valueLq, lq->smoothed_lq.valueNlq,
      lq->lq.valueLq, lq->lq.valueNlq);
#endif

    if (lq->smoothed_lq.valueLq < lq->lq.valueLq) {
      if (lq->lq.valueLq >= 254 || lq->lq.valueLq - lq->smoothed_lq.valueLq > lq->smoothed_lq.valueLq/10) {
        relevant = true;
      }
    }
    else if (lq->smoothed_lq.valueLq > lq->lq.valueLq) {
      if (lq->smoothed_lq.valueLq - lq->lq.valueLq > lq->smoothed_lq.valueLq/10) {
        relevant = true;
      }
    }
    if (lq->smoothed_lq.valueNlq < lq->lq.valueNlq) {
      if (lq->lq.valueNlq >= 254 || lq->lq.valueNlq - lq->smoothed_lq.valueNlq > lq->smoothed_lq.valueNlq/10) {
        relevant = true;
      }
    }
    else if (lq->smoothed_lq.valueNlq > lq->lq.valueNlq) {
      if (lq->smoothed_lq.valueNlq - lq->lq.valueNlq > lq->smoothed_lq.valueNlq/10) {
        relevant = true;
      }
    }

    if (relevant) {
      memcpy(&lq->smoothed_lq, &lq->lq, sizeof(struct default_lq_hybrid_plc));
      link->linkcost = default_lq_calc_cost_hybrid_plc(&lq->smoothed_lq);
      triggered = true;
    }
  } OLSR_FOR_ALL_LINK_ENTRIES_END(link)

  if (!triggered) {
    return;
  }

  OLSR_FOR_ALL_LINK_ENTRIES(link) {
    lq = (struct default_lq_hybrid_plc_hello *)link->linkquality;

    if (lq->smoothed_lq.valueLq >= 254 && lq->smoothed_lq.valueNlq >= 254) {
      continue;
    }

    if (lq->smoothed_lq.valueLq == lq->lq.valueLq && lq->smoothed_lq.valueNlq == lq->lq.valueNlq) {
      continue;
    }

    memcpy(&lq->smoothed_lq, &lq->lq, sizeof(struct default_lq_hybrid_plc));
    link->linkcost = default_lq_calc_cost_hybrid_plc(&lq->smoothed_lq);
  } OLSR_FOR_ALL_LINK_ENTRIES_END(link)

  olsr_relevant_linkcost_change();
}

static void
default_lq_parser_hybrid_plc(struct olsr *olsr, struct interface *in_if, union olsr_ip_addr *from_addr)
{
  const union olsr_ip_addr *main_addr;
  struct link_entry *lnk;
  struct default_lq_hybrid_plc_hello *lq;
  uint32_t seq_diff;

  /* Find main address */
  main_addr = mid_lookup_main_addr(from_addr);

  /* Loopup link entry */
  lnk = lookup_link_entry(from_addr, main_addr, in_if);
  if (lnk == NULL) {
    return;
  }

  lq = (struct default_lq_hybrid_plc_hello *)lnk->linkquality;

  /* ignore double package */
  if (lq->last_seq_nr == olsr->olsr_seqno) {
    struct ipaddr_str buf;
    olsr_syslog(OLSR_LOG_INFO, "detected duplicate packet with seqnr %d from %s on %s (%d Bytes)",
		olsr->olsr_seqno,olsr_ip_to_string(&buf, from_addr),in_if->int_name,ntohs(olsr->olsr_packlen));
    return;
  }

  if (lq->last_seq_nr > olsr->olsr_seqno) {
    seq_diff = (uint32_t) olsr->olsr_seqno + 65536 - lq->last_seq_nr;
  } else {
    seq_diff = olsr->olsr_seqno - lq->last_seq_nr;
  }

  /* Jump in sequence numbers ? */
  if (seq_diff > 256) {
    seq_diff = 1;
  }

  lq->received[lq->activePtr]++;
  lq->total[lq->activePtr] += seq_diff;

  lq->last_seq_nr = olsr->olsr_seqno;
  lq->missed_hellos = 0;
}

static void
default_lq_hybrid_plc_timer(void __attribute__ ((unused)) * context)
{
  struct link_entry *link;

  OLSR_FOR_ALL_LINK_ENTRIES(link) {
    struct default_lq_hybrid_plc_hello *tlq = (struct default_lq_hybrid_plc_hello *)link->linkquality;
    fpm ratio;
    int i, received, total;

    received = 0;
    total = 0;

    /* enlarge window if still in quickstart phase */
    if (tlq->windowSize < LQ_HYBRID_PLC_WINDOW) {
      tlq->windowSize++;
    }
    for (i = 0; i < tlq->windowSize; i++) {
      received += tlq->received[i];
      total += tlq->total[i];
    }

    /* calculate link quality */
    if (total == 0) {
      tlq->lq.valueLq = 0;
    } else {
      // start with link-loss-factor
      ratio = fpmidiv(itofpm(link->loss_link_multiplier), LINK_LOSS_MULTIPLIER);

      /* keep missed hello periods in mind (round up hello interval to seconds) */
      if (tlq->missed_hellos > 1) {
        received = received - received * tlq->missed_hellos * link->inter->hello_etime/1000 / LQ_HYBRID_PLC_WINDOW;
      }

      // calculate received/total factor
      ratio = fpmmuli(ratio, received);
      ratio = fpmidiv(ratio, total);
      ratio = fpmmuli(ratio, 255);

      tlq->lq.valueLq = (uint8_t) (fpmtoi(ratio));
    }
    OLSR_PRINTF(1, "Interface mode: %d\n", link->inter->mode);
    /* ethernet booster */
    if (link->inter->mode == IF_MODE_ETHER) {
      if (tlq->lq.valueLq > (uint8_t)(0.95 * 255)) {
        tlq->perfect_eth = true;
      }
      else if (tlq->lq.valueLq > (uint8_t)(0.90 * 255)) {
        tlq->perfect_eth = false;
      }

      if (tlq->perfect_eth) {
        tlq->lq.valueLq = 255;
      }
    }
    else if (link->inter->mode != IF_MODE_ETHER && tlq->lq.valueLq > 0) {
      tlq->lq.valueLq--;
    }

    // shift buffer
    tlq->activePtr = (tlq->activePtr + 1) % LQ_HYBRID_PLC_WINDOW;
    tlq->total[tlq->activePtr] = 0;
    tlq->received[tlq->activePtr] = 0;
  } OLSR_FOR_ALL_LINK_ENTRIES_END(link);

  default_lq_hybrid_plc_handle_lqchange();
}

static void
default_lq_initialize_hybrid_plc(void)
{
  if (olsr_cnf->lq_nat_thresh < 1.0) {
    fprintf(stderr, "Warning, nat_treshold < 1.0 is more likely to produce loops with etx_hybrid_plc\n");
  }
  olsr_packetparser_add_function(&default_lq_parser_hybrid_plc);
  olsr_start_timer(1000, 0, OLSR_TIMER_PERIODIC, &default_lq_hybrid_plc_timer, NULL, 0);
  int pid;
  char current_path[FILENAME_MAX];
  printf("*** Hybrid PLC: plugin_init\n");
  getcwd(current_path, sizeof(current_path));
  printf ("The current working directory is %s", current_path);

  if ((pid = fork()) == -1)
	  perror("fork error");
  else if (pid == 0) {
	  execlp("./lib/faifa_proxy/faifa_proxy.o", "faifa_proxy", NULL);
	  printf("Return not expected. Must be an execlp error.\n");
  } else {
	  printf("sono nel PLUGIN!!!\n");
	  olsr_start_timer(2 * MSEC_PER_SEC, 0, OLSR_TIMER_PERIODIC, &update_plc_data, NULL, 0);
  }

}

static olsr_linkcost
default_lq_calc_cost_hybrid_plc(const void *ptr)
{
  const struct default_lq_hybrid_plc *lq = ptr;
  olsr_linkcost cost;
  bool ether;
  int lq_int, nlq_int;

  if (lq->valueLq < (unsigned int)(255 * MINIMAL_USEFUL_LQ) || lq->valueNlq < (unsigned int)(255 * MINIMAL_USEFUL_LQ)) {
    return LINK_COST_BROKEN;
  }

  ether = lq->valueLq == 255 && lq->valueNlq == 255;

  lq_int = (int)lq->valueLq;
  if (lq_int > 0 && lq_int < 255) {
    lq_int++;
  }

  nlq_int = (int)lq->valueNlq;
  if (nlq_int > 0 && nlq_int < 255) {
    nlq_int++;
  }
  cost = fpmidiv(itofpm(255 * 255), lq_int * nlq_int);
  if (ether) {
    /* ethernet boost */
    cost /= 10;
  }

  if (cost > LINK_COST_BROKEN)
    return LINK_COST_BROKEN;
  if (cost == 0)
    return 1;
  return cost;
}

static int
default_lq_serialize_hello_lq_pair_hybrid_plc(unsigned char *buff, void *ptr)
{
  struct default_lq_hybrid_plc *lq = ptr;

  buff[0] = (unsigned char)(0);
  buff[1] = (unsigned char)(0);
  buff[2] = (unsigned char)lq->valueLq;
  buff[3] = (unsigned char)lq->valueNlq;

  return 4;
}

static void
default_lq_deserialize_hello_lq_pair_hybrid_plc(const uint8_t ** curr, void *ptr)
{
  struct default_lq_hybrid_plc *lq = ptr;

  pkt_ignore_u16(curr);
  pkt_get_u8(curr, &lq->valueLq);
  pkt_get_u8(curr, &lq->valueNlq);
}

static int
default_lq_serialize_tc_lq_pair_hybrid_plc(unsigned char *buff, void *ptr)
{
  struct default_lq_hybrid_plc *lq = ptr;

  buff[0] = (unsigned char)(0);
  buff[1] = (unsigned char)(0);
  buff[2] = (unsigned char)lq->valueLq;
  buff[3] = (unsigned char)lq->valueNlq;

  return 4;
}

static void
default_lq_deserialize_tc_lq_pair_hybrid_plc(const uint8_t ** curr, void *ptr)
{
  struct default_lq_hybrid_plc *lq = ptr;

  pkt_ignore_u16(curr);
  pkt_get_u8(curr, &lq->valueLq);
  pkt_get_u8(curr, &lq->valueNlq);
}

static void
default_lq_packet_loss_worker_hybrid_plc(struct link_entry *link,
    void __attribute__ ((unused)) *ptr, bool lost)
{
  struct default_lq_hybrid_plc_hello *tlq = (struct default_lq_hybrid_plc_hello *)link->linkquality;

  if (lost) {
    tlq->missed_hellos++;
  }
  return;
}

static void
default_lq_memorize_foreign_hello_hybrid_plc(void *ptrLocal, void *ptrForeign)
{
  struct default_lq_hybrid_plc_hello *local = ptrLocal;
  struct default_lq_hybrid_plc *foreign = ptrForeign;

  if (foreign) {
    local->lq.valueNlq = foreign->valueLq;
  } else {
    local->lq.valueNlq = 0;
  }
}

static void
default_lq_copy_link2neigh_hybrid_plc(void *t, void *s)
{
  struct default_lq_hybrid_plc *target = t;
  struct default_lq_hybrid_plc_hello *source = s;
  *target = source->smoothed_lq;
}

static void
default_lq_copy_link2tc_hybrid_plc(void *t, void *s)
{
  struct default_lq_hybrid_plc *target = t;
  struct default_lq_hybrid_plc_hello *source = s;
  *target = source->smoothed_lq;
}

static void
default_lq_clear_hybrid_plc(void *target)
{
  memset(target, 0, sizeof(struct default_lq_hybrid_plc));
}

static void
default_lq_clear_hybrid_plc_hello(void *target)
{
  struct default_lq_hybrid_plc_hello *local = target;
  int i;

  default_lq_clear_hybrid_plc(&local->lq);
  default_lq_clear_hybrid_plc(&local->smoothed_lq);
  local->windowSize = LQ_HYBRID_PLC_QUICKSTART_INIT;
  for (i = 0; i < LQ_HYBRID_PLC_WINDOW; i++) {
    local->total[i] = 3;
  }
}

static const char *
default_lq_print_hybrid_plc(void *ptr, char separator, struct lqtextbuffer *buffer)
{
  struct default_lq_hybrid_plc *lq = ptr;
  int lq_int, nlq_int;

  lq_int = (int)lq->valueLq;
  if (lq_int > 0 && lq_int < 255) {
    lq_int++;
  }

  nlq_int = (int)lq->valueNlq;
  if (nlq_int > 0 && nlq_int < 255) {
    nlq_int++;
  }

  snprintf(buffer->buf, sizeof(buffer->buf), "%s%c%s", fpmtoa(fpmidiv(itofpm(lq_int), 255)), separator,
           fpmtoa(fpmidiv(itofpm(nlq_int), 255)));
  return buffer->buf;
}

static const char *
default_lq_print_cost_hybrid_plc(olsr_linkcost cost, struct lqtextbuffer *buffer)
{
  snprintf(buffer->buf, sizeof(buffer->buf), "%s", fpmtoa(cost));
  return buffer->buf;
}

static void deserialize_stations_data(unsigned char *buff) {
	int i;
	printf("Alloco: %lu\n", p_size_t * sizeof(*p_data));
	p_data = (struct plc_data *) malloc(p_size_t * sizeof(*p_data));
	for (i = 0; i < p_size_t; i++) {
		p_data[i].mac[0] = buff[0 + 8 * i];
		p_data[i].mac[1] = buff[1 + 8 * i];
		p_data[i].mac[2] = buff[2 + 8 * i];
		p_data[i].mac[3] = buff[3 + 8 * i];
		p_data[i].mac[4] = buff[4 + 8 * i];
		p_data[i].mac[5] = buff[5 + 8 * i];
		p_data[i].lq = buff[6 + 8 * i];
		p_data[i].nlq = buff[7 + 8 * i];
	}
}

static void update_plc_data(void) {
	const char* const socket_name = "faifaproxy";
	int error;
	/* Create the socket.  */
	socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	/* Store the server's name in the socket address.  */
	name.sun_family = AF_LOCAL;
	strcpy(name.sun_path, socket_name);
	printf("socket plugin: %s\n", name.sun_path);

	/* Connect the socket.  */
	error = 1;
	while (error != 0) {
		error = connect(socket_fd, &name, SUN_LEN (&name));
		if (error == 0) {
			printf("socket ok!\n");
			printf("update PLC data!\n");
			char m = 'p';
			u_int8_t n_stas;
			int length;
			unsigned char *buff;
			send(socket_fd, &m, sizeof(m), 0);
			printf("Poll mandato!\n");
			if (recv(socket_fd, &p_size_t, sizeof(p_size_t), 0) == 0) {
				printf("Non ricevo p_size_t!\n");
				//return;
			}
			printf("Numero di stazioni presenti ricevuto: %d\n", p_size_t);
			length = p_size_t * sizeof(struct plc_data);
			buff = (unsigned char*) malloc(length);
			if (recv(socket_fd, buff, length, 0) == 0) {
				printf("Non ricevo buff!\n");
				//return;
			}
			recv(socket_fd, buff, length, 0);
			deserialize_stations_data(buff);
			//print_plc_data();
			close(socket_fd);

		} else {
			printf("ERROR: %s\n", strerror(errno));
		}
	}
}



/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
