/*
 * faifa_proxy.c
 *
 *  Created on: Dec 13, 2010
 *      Author: kinto
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <net/ethernet.h>

#include <faifa.h>
#include <homeplug.h>
#include <homeplug_av.h>
#include <endian.h>

extern FILE *err_stream;
extern FILE *out_stream;
extern FILE *in_stream;

faifa_t *faifa;
u_int8_t hpav_intellon_oui[3] = { 0x00, 0xB0, 0x52 };
u_int8_t hpav_intellon_macaddr[ETHER_ADDR_LEN] = { 0x00, 0xB0, 0x52, 0x00,
		0x00, 0x01 };



struct plc_data {
	u_int8_t mac[6];
	u_int8_t lq;
	u_int8_t nlq;
};

struct station_data {
	struct sta_info sta_info;
	float raw_rate; //raw rate obtained with modulation data from A071 frame
};

struct network_data {
	int station_count;
	u_int8_t network_id[7];
	u_int8_t sta_mac[6];
	u_int8_t sta_role;
	u_int8_t cco_mac[6];
	float tx_mpdu_collision_perc;
	float tx_mpdu_failure_perc;
	float tx_pb_failure_perc;
	float rx_mpdu_failure_perc;
	float rx_pb_failure_perc;
	float rx_tbe_failure_perc;
	struct station_data sta_data[];
};


struct plc_data *p_data;
u_int8_t p_size_t;

static void error(char *message) {
	fprintf(stderr, "%s: %s\n", "faifa", message);
}

int dump_hex(void *buf, int len, char *sep) {
	int avail = len;
	u_int8_t *p = buf;

	while (avail > 0) {
		faifa_printf(out_stream, "%02hX%s", *p, (avail > 1) ? sep : "");
		p++;
		avail--;
	}
	return len;
}

#define HEX_BLOB_BYTES_PER_ROW  16

void print_blob(u_char *buf, int len) {
	u_int32_t i, d, m = len % HEX_BLOB_BYTES_PER_ROW;
	faifa_printf(out_stream, "Binary Data, %lu bytes", (unsigned long int) len);
	for (i = 0; i < len; i += HEX_BLOB_BYTES_PER_ROW) {
		d = (len - i) / HEX_BLOB_BYTES_PER_ROW;
		faifa_printf(out_stream, "\n%08lu: ", (unsigned long int) i);
		dump_hex((u_int8_t *) buf + i, (d > 0) ? HEX_BLOB_BYTES_PER_ROW : m," ");
	}
	faifa_printf(out_stream, "\n");
}

void print_plc_data() {
	int i;
	for (i=0; i<p_size_t; i++) {
		 printf("Station MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", p_data[i].mac[0], p_data[i].mac[1], p_data[i].mac[2], p_data[i].mac[3], p_data[i].mac[4], p_data[i].mac[5]);
		 printf("LQ: %d\n", p_data[i].lq);
		 printf("NLQ: %d\n\n", p_data[i].nlq);
	}
}

int get_bits_per_carrier(short unsigned int modulation) {
	switch (modulation) {
	case NO:
		return 0;
		break;
	case BPSK:
		return 1;
		break;
	case QPSK:
		return 2;
		break;
	case QAM_8:
		return 3;
		break;
	case QAM_16:
		return 4;
		break;
	case QAM_64:
		return 6;
		break;
	case QAM_256:
		return 8;
		break;
	case QAM_1024:
		return 10;
		break;
	default:
		return 0;
		break;
	}
}

int init_frame(u_int8_t *frame_buf, int frame_len, u_int16_t mmtype) {
	struct hpav_frame *frame;
	u_int8_t *frame_ptr = frame_buf;
	bzero(frame_buf, frame_len);
	u_int8_t *da;
	da = hpav_intellon_macaddr;
	u_int8_t *sa = NULL;
	/* Set the ethernet frame header */
	int n;
	n = ether_init_header(frame_ptr, frame_len, da, sa, ETHERTYPE_HOMEPLUG_AV);
	frame_ptr += n;
	frame = (struct hpav_frame *) frame_ptr;
	n = sizeof(frame->header);
	frame->header.mmtype = STORE16_LE(mmtype);
	if ((mmtype & HPAV_MM_CATEGORY_MASK) == HPAV_MM_VENDOR_SPEC) {
		frame->header.mmver = HPAV_VERSION_1_0;
		memcpy(frame->payload.vendor.oui, hpav_intellon_oui, 3);
		n += sizeof(frame->payload.vendor);
	} else {
		frame->header.mmver = HPAV_VERSION_1_1;
		n += sizeof(frame->payload.public);
	}
	frame_ptr += n;
	frame_len = frame_ptr - (u_int8_t *) frame_buf;
	return frame_len;
}

int send_A038(u_int8_t *frame_buf, int frame_len, int cursor) {
	u_int8_t *frame_ptr = frame_buf;
	frame_ptr += cursor;
	frame_len = frame_ptr - (u_int8_t *) frame_buf;
	if (frame_len < ETH_ZLEN)
		frame_len = ETH_ZLEN;
	frame_len = faifa_send(faifa, frame_buf, frame_len);
	if (frame_len == -1)
		faifa_printf(err_stream, "Init: error sending frame (%s)\n",
				faifa_error(faifa));
	return frame_len;
}

int send_A070(u_int8_t *frame_buf, int frame_len, int cursor, u_int8_t macaddr[], u_int8_t tsslot) {
	u_int8_t *frame_ptr = frame_buf;
	int i, n;
	frame_ptr += cursor;
	struct get_tone_map_charac_request *mm =
			(struct get_tone_map_charac_request *) frame_ptr;
	for (i = 0; i < 6; i++)
		mm->macaddr[i] = macaddr[i];
	n = sizeof(*mm);
	frame_len = frame_ptr - (u_int8_t *) frame_buf;
	if (frame_len < ETH_ZLEN)
		frame_len = ETH_ZLEN;
	frame_len = faifa_send(faifa, frame_buf, frame_len);
	if (frame_len == -1)
		faifa_printf(err_stream, "Init: error sending frame (%s)\n",
				faifa_error(faifa));
	return frame_len;
}

void hpav_cast_frame(u_int8_t *frame_ptr, int frame_len, struct ether_header *hdr) {
	struct hpav_frame *frame = (struct hpav_frame *) frame_ptr;
	if ((frame->header.mmtype & HPAV_MM_CATEGORY_MASK) == HPAV_MM_VENDOR_SPEC) {
		frame_ptr = frame->payload.vendor.data;
		frame_len -= sizeof(frame->payload.vendor);
	} else {
		frame_ptr = frame->payload.public.data;
		frame_len -= sizeof(frame->payload.public);
	}
	switch (frame->header.mmtype) {
	case 0xA039: {
		struct network_info_confirm *mm =
				(struct network_info_confirm *) frame_ptr;
		int n_stas = mm->num_stas;
		p_size_t = n_stas;
		p_data = (struct plc_data *) malloc(n_stas * sizeof(*p_data));
		//**nd = (struct network_data *) malloc(sizeof(struct network_data)+ sizeof(struct station_data[n_stas]));
		//(**nd)->station_count = n_stas;
		//memcpy((**nd)->network_id, mm->nid, 7);
		//memcpy((**nd)->sta_mac, hdr->ether_shost, ETHER_ADDR_LEN);
		//(**nd)->sta_role = mm->sta_role;
		//memcpy((**nd)->cco_mac, mm->cco_macaddr, ETHER_ADDR_LEN);
		int i;
		//struct station_data sta_d[n_stas];

		for (i = 0; i < n_stas; i++) {
			memcpy(&p_data[i].mac, mm->stas[i].sta_macaddr, sizeof(mm->stas[i].sta_macaddr));
			p_data[i].lq = mm->stas[i].avg_phy_tx_rate;
			p_data[i].nlq = mm->stas[i].avg_phy_rx_rate;
			//sta_d[i].sta_info = mm->stas[i];
			//memcpy(&sta_d[i].sta_info, &mm->stas[i], sizeof(struct sta_info));
			//(**nd)->sta_data[i] = sta_d[i];
		}
		break;
	}
	case 0xA031:

		break;
	case 0xA071: {
		/*faifa_printf(out_stream, "ricevo A071\n");
		struct get_tone_map_charac_confirm *mm =
				(struct get_tone_map_charac_confirm *) frame_ptr;
		if (mm->mstatus != 0x00) {
			faifa_printf(out_stream, "A070-A071 error\n");
			break;
		}
		unsigned int total_bits = 0;
		int i;
		for (i = 0; i < MAX_CARRIERS; i++) {
			//faifa_printf(out_stream, "Total bits: %d\n", total_bits);
			total_bits += get_bits_per_carrier(mm->carriers[i].mod_carrier_lo);
			total_bits += get_bits_per_carrier(mm->carriers[i].mod_carrier_hi);
		}
		//faifa_printf(out_stream, "Total bits: %d\n", total_bits);
		float bits_per_second = (float) total_bits / 0.0000465;
		faifa_printf(out_stream, "Modulation rate: %4.2f bit/s\n",
				bits_per_second);
		break;*/
	}
	}
}

void receive_frame() {
	u_int8_t *buf;

	u_int32_t l = 1518;
	u_int16_t *eth_type;
	do {
		l = 1518;
		l = faifa_recv(faifa, buf, l);
		struct ether_header *eth_header = (struct ether_header *) buf;
		eth_type = &(eth_header->ether_type);
		u_int8_t *frame_ptr = (u_int8_t *) buf, *payload_ptr;
		int frame_len = l, payload_len;
		payload_ptr = frame_ptr + sizeof(*eth_header);
		payload_len = frame_len - sizeof(*eth_header);
		//if((*eth_type == ntohs(ETHERTYPE_HOMEPLUG)) || (*eth_type == ntohs(ETHERTYPE_HOMEPLUG_AV))) {
		if ((*eth_type == ntohs(ETHERTYPE_HOMEPLUG_AV))) {
			hpav_cast_frame(payload_ptr, payload_len, eth_header);
			//print_blob(frame_ptr, frame_len);
		}
	} while (!(*eth_type == ntohs(ETHERTYPE_HOMEPLUG)) && !(*eth_type == ntohs(ETHERTYPE_HOMEPLUG_AV)));
}

void update_PLC_data() {
	int c, s;
	u_int8_t frame_buf[1518];
	int frame_len = sizeof(frame_buf);
	c = init_frame(frame_buf, frame_len, 0xA038);
	s = send_A038(frame_buf, frame_len, c);
	receive_frame();
	print_plc_data();
}

int set_timer(struct timeval *tv, time_t sec) {
	gettimeofday(tv, NULL);
	tv->tv_sec += sec;
	return 1;
}

int check_timer(struct timeval tv, time_t sec) {
	struct timeval ctv;
	gettimeofday(&ctv, NULL);

	if (ctv.tv_sec > tv.tv_sec) {
		gettimeofday(&tv, NULL);
		tv.tv_sec += sec;
		return 1;
	} else
		return 0;
}

int serialize_stations_data(unsigned char *buff) {
	int i;
	for (i = 0; i < p_size_t; i++) {
		buff[0 + 8 * i] = p_data[i].mac[0];
		buff[1 + 8 * i] = p_data[i].mac[1];
		buff[2 + 8 * i] = p_data[i].mac[2];
		buff[3 + 8 * i] = p_data[i].mac[3];
		buff[4 + 8 * i] = p_data[i].mac[4];
		buff[5 + 8 * i] = p_data[i].mac[5];
		buff[6 + 8 * i] = p_data[i].lq;
		buff[7 + 8 * i] = p_data[i].nlq;
	}
}

int server(int client_socket) {
	u_int8_t msg;
	printf("Nel servente...\n");
	if (recv(client_socket, &msg, sizeof(msg), 0) == 0)
		return 0;
	if (msg == 'q') {
		return 1;
	}
	if (msg == 'p') {
		printf("faifa_proxy: POLLED!\n");
		printf("Numero di stazioni da mandare: %d\n", p_size_t);
		int length = p_size_t * sizeof(struct plc_data);
		unsigned char buff[length];
		serialize_stations_data(&buff);
		int i;
		for (i = 0; i < sizeof(buff); i++) {
			printf("Buff[%d]: %x\n", i, buff[i]);
		}
		send(client_socket, &p_size_t, sizeof(p_size_t), 0);
		send(client_socket, buff, length, 0);
		return 0;
	}
}

int main(int argc, char **argv) {
	char *opt_ifname = NULL;
	out_stream = stdout;
	err_stream = stderr;
	in_stream = stdin;
	opt_ifname = "eth0";

	faifa = faifa_init();
	if (faifa == NULL) {
		error("can't initialize Faifa library");
		return -1;
	}
	if (faifa_open(faifa, opt_ifname) == -1) {
		error("error opening interface");
		faifa_free(faifa);
		return -1;
	}
	faifa_printf(out_stream, "in main\n");
	update_PLC_data();
	//inizializza socket locale
	const char* const socket_name = "faifaproxy";
	int socket_fd;
	struct sockaddr_un name;
	int client_sent_quit_message = 0;

	/* Create the socket.  */
	socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	/* Indicate this is a server.  */
	name.sun_family = AF_LOCAL;
	strcpy(name.sun_path, socket_name);
	//printf("socket faifaproxy: %s\n", name.sun_path);
	bind(socket_fd, &name, SUN_LEN(&name));
	/* Listen for connections.  */
	listen(socket_fd, 1);

	struct timeval ts, tv;
	//crea il timer per l'update dei dati dal livello PLC (ogni 5 secondi)
	set_timer(&tv, 5);
	do {
		struct sockaddr_un client_name;
		socklen_t client_name_len;
		int client_socket_fd;
		fd_set rfds;
		int retval, maxfd;
		FD_ZERO(&rfds);
		FD_SET(socket_fd, &rfds);

		ts.tv_sec = 4;
		ts.tv_usec = 0;
		retval = 1;
		maxfd = socket_fd + 1;
		retval = select(maxfd, &rfds, NULL, NULL, &ts);
		/* Don't rely on the value of tv now! */

		if (retval == -1)
			perror("select()");
		else if (retval) {
			printf("Prima di accept.\n");
			client_socket_fd = accept(socket_fd, &client_name, &client_name_len);
			/* Handle the connection.  */
			client_sent_quit_message = server(client_socket_fd);
			/* Close our end of the connection.  */
			close(client_socket_fd);
			/* FD_ISSET(0, &rfds) will be true. */
		} else
			printf("Non ho ricevuto nulla nell'arco di 4 secondi.\n");
		if (check_timer(tv, 5) == 1) {
			set_timer(&tv, 5);
			printf("timer scaduto\n");
			update_PLC_data();
		}
	} while (!client_sent_quit_message);
	unlink(name.sun_path);
	faifa_printf(out_stream, "Ricevuto. Chiudo\n");
	faifa_close(faifa);
	faifa_free(faifa);
	return 0;
}
