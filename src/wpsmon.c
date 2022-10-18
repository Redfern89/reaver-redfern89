/*
 * Wash - Main and usage functions
 * Copyright (c) 2011, Tactical Network Solutions, Craig Heffner <cheffner@tacnetsol.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU General Public License in all respects
 *  for all of the code used other than OpenSSL. *  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so. *  If you
 *  do not wish to do so, delete this exception statement from your
 *  version. *  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 */

#include "colors.h"
#include "wpsmon_progress.h"
#include "wpsmon.h"
#include "utils/file.h"
#include "utils/vendor.h"
#include "send.h"
#include <fcntl.h>
#include <math.h>

#define MAX_APS 512

extern const char* get_version(void);
static void wash_usage(char *prog);

int show_all_aps = 0;
int json_mode = 0;
int show_utf8_ssid = 0;
int show_crack_progress = 0;
int show_no_lck = 0;
int use_colors = 0;
int rssi_min = 99;

static struct mac {
	unsigned char mac[6];
	unsigned char vendor_oui[1+3];
	unsigned char probes;
	unsigned char flags;
} seen_list[MAX_APS];
enum seen_flags {
	SEEN_FLAG_PRINTED = 1,
	SEEN_FLAG_COMPLETE = 2,
	SEEN_FLAG_PBC = 4,
	SEEN_FLAG_LOCKED = 8,
	SEEN_FLAG_WPS_ACTIVE = 16,
};
static unsigned seen_count;
static int list_insert(char *bssid) {
	unsigned i;
	unsigned char mac[6];
	str2mac(bssid, mac);
	for(i=0; i<seen_count; i++)
		if(!memcmp(seen_list[i].mac, mac, 6)) return i;
	if(seen_count >= MAX_APS) {
		memset(seen_list, 0, sizeof seen_list);
		seen_count = 0;
	}
	memcpy(seen_list[seen_count].mac, mac, 6);
	return seen_count++;
}
static int is_pbc(struct libwps_data *wps) {
	int active = 0;
	if (*wps->selected_registrar && atoi(wps->selected_registrar) == 1 && *wps->device_password_id && atoi(wps->device_password_id) == 4) {
		active = 1;
	}
	return active;
}
static void reset_flag_complete_printed(int x) {
	seen_list[x].flags &= ~SEEN_FLAG_COMPLETE;
	seen_list[x].flags &= ~SEEN_FLAG_PRINTED;
}
static void set_flag_wps(int x, int active, enum seen_flags flag) {
	if (seen_list[x].flags & flag) {
		if (!active) {
			reset_flag_complete_printed(x);
			seen_list[x].flags &= ~flag;
		}
	} else {
		if (active) {
			reset_flag_complete_printed(x);
			seen_list[x].flags |= flag;
		}
	}
}
static int was_printed(char* bssid) {
	int x = list_insert(bssid);
	if(x >= 0 && x < MAX_APS) {
		unsigned f = seen_list[x].flags;
		seen_list[x].flags |= SEEN_FLAG_PRINTED;
		return f & SEEN_FLAG_PRINTED;
	}
	return 1;
}
static void mark_ap_complete(char *bssid) {
	int x = list_insert(bssid);
	if(x >= 0 && x < MAX_APS) seen_list[x].flags |= SEEN_FLAG_COMPLETE;
}
static int is_done(char *bssid, struct libwps_data *wps) {
	int x = list_insert(bssid);
	if(x >= 0 && x < MAX_APS) {
		if(wps) {
			set_flag_wps(x, is_pbc(wps), SEEN_FLAG_PBC);
			set_flag_wps(x, wps->locked == WPSLOCKED, SEEN_FLAG_LOCKED);
			set_flag_wps(x, 1, SEEN_FLAG_WPS_ACTIVE);
		} else {
			set_flag_wps(x, 0, SEEN_FLAG_WPS_ACTIVE);
		}
		return seen_list[x].flags & SEEN_FLAG_COMPLETE;
	}
	return 1;
}
static int should_probe(char *bssid) {
	int x = list_insert(bssid);
        if(x >= 0 && x < MAX_APS) return seen_list[x].probes < get_max_num_probes();
	return 0;
}
static void update_probe_count(char *bssid) {
	int x = list_insert(bssid);
	if(x >= 0 && x < MAX_APS) seen_list[x].probes++;
}
static void set_ap_vendor(char *bssid) {
	int x = list_insert(bssid);
	if(x >= 0 && x < MAX_APS) memcpy(seen_list[x].vendor_oui, globule->vendor_oui, sizeof(seen_list[x].vendor_oui));
}
static unsigned char *get_ap_vendor(char* bssid) {
	int x = list_insert(bssid);
	if(x >= 0 && x < MAX_APS && seen_list[x].vendor_oui[0])
		return seen_list[x].vendor_oui+1;
	return 0;
}

static volatile int got_sigint;
static void sigint_handler(int x) {
	(void) x;
	got_sigint = 1;
	pcap_breakloop(get_handle());
}

int wash_main(int argc, char *argv[])
{
	int c = 0;
	FILE *fp = NULL;
	int long_opt_index = 0, i = 0, channel = 0, passive = 0, mode = 0;
	int source = INTERFACE, ret_val = EXIT_FAILURE;
	struct bpf_program bpf = { 0 };
	char *last_optarg = NULL, *target = NULL, *bssid = NULL;
	char *short_options = "i:c:n:b:O:R:25sfuFDhajUpZC";
        struct option long_options[] = {
		{ "bssid", required_argument, NULL, 'b' },
                { "interface", required_argument, NULL, 'i' },
                { "channel", required_argument, NULL, 'c' },
		{ "probes", required_argument, NULL, 'n' },
		{ "output-file", required_argument, NULL, 'O'},
		{ "rssi-min", required_argument, NULL, 'R' },
		{ "file", no_argument, NULL, 'f' },
		{ "ignore-fcs", no_argument, NULL, 'F' },
		{ "2ghz", no_argument, NULL, '2' },
		{ "5ghz", no_argument, NULL, '5' },
		{ "scan", no_argument, NULL, 's' },
		{ "survey", no_argument, NULL, 'u' },
		{ "all", no_argument, NULL, 'a' },
		{ "json", no_argument, NULL, 'j' },
		{ "utf8", no_argument, NULL, 'U' },
		{ "no-lck", no_argument, NULL, 'Z' },
		{ "progress", no_argument, NULL, 'p' },
		{ "colors", no_argument, NULL, 'C' },
                { "help", no_argument, NULL, 'h' },
                { 0, 0, 0, 0 }
        };

	globule_init();
	set_auto_channel_select(0);
	set_wifi_band(0);
	set_debug(INFO);
	set_validate_fcs(1);
	/* send all warnings, etc to stderr */
	set_log_file(stderr);
	set_max_num_probes(DEFAULT_MAX_NUM_PROBES);

	setvbuf(stdout, 0, _IONBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);

	while((c = getopt_long(argc, argv, short_options, long_options, &long_opt_index)) != -1)
        {
                switch(c)
                {
			case 'f':
				source = PCAP_FILE;
				break;
			case 'i':
				set_iface(optarg);
				break;
			case 'b':
				bssid = strdup(optarg);
				break;
			case 'c':
				channel = atoi(optarg);
				set_fixed_channel(1);
				break;
			case 'O':
				{
					int ofd = open(optarg, O_WRONLY|O_CREAT|O_TRUNC, 0660);
					set_output_fd(ofd);
					if(ofd == -1) perror("open outputfile failed: ");
				}
				break;
			case '5':
				set_wifi_band(get_wifi_band() | AN_BAND);
				break;
			case '2':
				set_wifi_band(get_wifi_band() | BG_BAND);
				break;
			case 'n':
				set_max_num_probes(atoi(optarg));
				break;
			case 'j':
				json_mode = 1;
				break;
			case 's':
				mode = SCAN;
				break;
			case 'u':
				mode = SURVEY;
				break;
			case 'F':
				set_validate_fcs(0);
				break;
			case 'a':
				show_all_aps = 1;
				break;
			case 'U':
				show_utf8_ssid = 1;
				break;
			case 'p':
				show_crack_progress = 1;
				break;
			case 'Z':
				show_no_lck = 1;
				break;
			case 'R':
				rssi_min = atoi(optarg);
				break;
			case 'C':
				use_colors = 1;
				break;
			default:
				wash_usage(argv[0]);
				goto end;
		}

		/* Track the last optarg. This is used later when looping back through any specified pcap files. */
		if(optarg)
		{
			if(last_optarg)
			{
				free(last_optarg);
			}

			last_optarg = strdup(optarg);
		}
	}

	if(get_wifi_band() == 0) set_wifi_band(BG_BAND);

	/* The interface value won't be set if capture files were specified; else, there should have been an interface specified */
	if(!get_iface() && source != PCAP_FILE)
	{
		wash_usage(argv[0]);
		goto end;
	}
	else if(get_iface())
	{
		/* Get the MAC address of the specified interface */
		read_iface_mac();
	}

	if(get_iface() && source == PCAP_FILE)
	{
		cprintf(CRITICAL, "[X] ERROR: -i and -f options cannot be used together.\n");
		wash_usage(argv[0]);
		goto end;
	}

	/* If we're reading from a file, be sure we don't try to transmit probe requests */
	if(source == PCAP_FILE)
	{
		passive = 1;
	}

	/* 
	 * Loop through all of the specified capture sources. If an interface was specified, this will only loop once and the
	 * call to monitor() will block indefinitely. If capture files were specified, this will loop through each file specified
	 * on the command line and monitor() will return after each file has been processed.
	 */
	for(i=argc-1; i>0; i--)
	{
		/* If the source is a pcap file, get the file name from the command line */
		if(source == PCAP_FILE)
		{
			/* If we've gotten to the arguments, we're done */
			if((argv[i][0] == '-') ||
			   (last_optarg && (memcmp(argv[i], last_optarg, strlen(last_optarg)) == 0))
			)
			{
				break;
			}
			else
			{
				target = argv[i];
			}
		}
		/* Else, use the specified interface name */
		else
		{
			target = get_iface();
		}

		set_handle(capture_init(target));
		if(!get_handle())
		{
			cprintf(CRITICAL, "[X] ERROR: Failed to open '%s' for capturing\n", get_iface());
			goto end;
		}

		/* Do it. */
		monitor(bssid, passive, source, channel, mode);
		printf("\n");
	}

	ret_val = EXIT_SUCCESS;

end:
	globule_deinit();
	if(bssid) free(bssid);
	if(wpsmon.fp) fclose(wpsmon.fp);
	return ret_val;
}

/* Monitors an interface (or capture file) for WPS data in beacon packets or probe responses */
void monitor(char *bssid, int passive, int source, int channel, int mode)
{
	struct sigaction act;
	struct itimerval timer;
	struct pcap_pkthdr header;
	static int header_printed;
        const u_char *packet = NULL;

        memset(&act, 0, sizeof(struct sigaction));
        memset(&timer, 0, sizeof(struct itimerval));

	/* If we aren't reading from a pcap file, set the interface channel */
	if(source == INTERFACE)
	{
		/* 
		 * If a channel has been specified, set the interface to that channel. 
		 * Else, set a recurring 1 second timer that will call sigalrm() and switch to 
		 * a new channel.
		 */
		if(channel > 0)
		{
			change_channel(channel);
		}
		else
		{
        		act.sa_handler = sigalrm_handler;
        		sigaction (SIGALRM, &act, 0);

			/* Create the timer. */
			struct sigevent sev;
			struct itimerspec its;
			sev.sigev_notify = SIGEV_SIGNAL;
			sev.sigev_signo = SIGALRM;
			sev.sigev_value.sival_ptr = &globule->timer_id;
			timer_create(CLOCK_REALTIME, &sev, &globule->timer_id);
			/* Start the timer. */
			its.it_value.tv_sec = CHANNEL_INTERVAL / 1000000;
			its.it_value.tv_nsec = (CHANNEL_INTERVAL % 1000000) * 1000;
			its.it_interval.tv_sec = its.it_value.tv_sec;
			its.it_interval.tv_nsec = its.it_value.tv_nsec;
			timer_settime(globule->timer_id, 0, &its, NULL);

			int startchan = 1;
			if(get_wifi_band() == AN_BAND)
				startchan = 34;
			change_channel(startchan);
		}

		memset(&act, 0, sizeof(struct sigaction));
		sigaction (SIGINT, 0, &act);
		act.sa_flags &= ~SA_RESTART;
		act.sa_handler = sigint_handler;
		sigaction (SIGINT, &act, 0);

	}

	if(!header_printed)
	{
		if(!json_mode) {
			use_colors ? fprintf(stdout, "%s", HEAD_COLOR) : fprintf(stdout, "%s", RESET_COLOR);
			if (!show_no_lck) {
				if (show_crack_progress) {
					fprintf  (stdout, "BSSID               Ch  dBm  WPS  Lck  Vendor    Progr  ESSID\n");
				} else {
					fprintf  (stdout, "BSSID               Ch    Signal strength   dBm  WPS  Lck  Vendor    ESSID\n");
				}
			} else {
				if (show_crack_progress) {
					fprintf  (stdout, "BSSID               Ch  dBm  WPS  Vendor    Progr  ESSID\n");
				} else {
					fprintf  (stdout, "BSSID               Ch    Signal strength   dBm  WPS  Vendor    ESSID\n");
				}
				
			}
			//fprintf(stdout, "00:11:22:33:44:55  104  -77  1.0  Yes  Bloatcom  0123456789abcdef0123456789abcdef\n");
			use_colors ? fprintf(stdout, "%s", LINE_COLOR) : fprintf(stdout, "%s", RESET_COLOR);
			fprintf  (stdout, "--------------------------------------------------------------------------------\n");
			fprintf  (stdout, RESET_COLOR);
		}
		header_printed = 1;
	}

	while(!got_sigint && (packet = next_packet(&header))) {
		parse_wps_settings(packet, &header, bssid, passive, mode, source);
		memset((void *) packet, 0, header.len);
	}

	return;
}

#define wps_active(W) (((W)->version) || ((W)->locked != 2) || ((W)->state))

#define BEACON_SIZE(rth_len) (rth_len + sizeof(struct dot11_frame_header) + sizeof(struct beacon_management_frame))
/* probe responses, just like beacons, start their management frame packet with the same
   fixed parameters of size 12 */
#define PROBE_RESP_SIZE(rth_len) BEACON_SIZE(rth_len)

void parse_wps_settings(const u_char *packet, struct pcap_pkthdr *header, char *target, int passive, int mode, int source)
{
	struct libwps_data *wps = NULL;
	enum encryption_type encryption = NONE;
	char *bssid = NULL, *ssid = NULL, *lock_display = NULL;
	char *crack_progress = NULL;
	int wps_parsed = 0, probe_sent = 0, channel = 0, rssi = 0;
	static int channel_changed = 0;
	
	char *rssi_color = NULL;
	int lck_wps = 0;
	int dbm = 0;
	
	if(packet == NULL || header == NULL) goto end;

	struct radio_tap_header *rt_header = (void *) radio_header(packet, header->len);
	size_t rt_header_len = end_le16toh(rt_header->len);
	if(header->len < rt_header_len + sizeof(struct dot11_frame_header)) goto end;

	struct dot11_frame_header *frame_header = (void *) (packet + rt_header_len);

	unsigned f_type = frame_header->fc & end_htole16(IEEE80211_FCTL_FTYPE);
	unsigned fsub_type = frame_header->fc & end_htole16(IEEE80211_FCTL_STYPE);

	int is_management_frame = f_type == end_htole16(IEEE80211_FTYPE_MGMT);
	int is_beacon = is_management_frame && fsub_type == end_htole16(IEEE80211_STYPE_BEACON);
	int is_probe_resp = is_management_frame && fsub_type == end_htole16(IEEE80211_STYPE_PROBE_RESP);

	if(!(is_probe_resp || is_beacon)) goto end;

	if(is_beacon && header->len < BEACON_SIZE(rt_header_len)) goto end;
	if(is_probe_resp && header->len < PROBE_RESP_SIZE(rt_header_len)) goto end;

	/* If a specific BSSID was specified, only parse packets from that BSSID */
	if(memcmp(get_bssid(), NULL_MAC, MAC_ADDR_LEN) &&
	   !is_target(frame_header)) goto end;

	wps = malloc(sizeof(struct libwps_data));
	memset(wps, 0, sizeof(struct libwps_data));


	set_ssid(NULL);
	bssid = (char *) mac2str(frame_header->addr3, ':');
	set_bssid((unsigned char *) frame_header->addr3);

	if(bssid)
	{
		if((target == NULL) ||
		   (target != NULL && strcmp(bssid, target) == 0))
		{
			channel = parse_beacon_tags(packet, header->len);
			if(channel == 0) {
				channel = freq_to_chan(rt_channel_freq(packet, header->len));
				/* If we didn't get channel from tagged IEs nor radiotap, we take it from the current chan of the scan */
				if(!channel) channel = get_channel();
			}
			rssi = signal_strength(packet, header->len);
			ssid = (char *) get_ssid();
			
			dbm = -rssi;
			
			int wps_signal_progess_idx = floor(((dbm - dbm_max) * dbm_array_max_idx) / (dbm_min - dbm_max));
			
			if (use_colors) {
				if (dbm >= 30 && dbm <= 69) rssi_color = COLOR_GREEN;
				if (dbm >= 70 && dbm <= 85) rssi_color = COLOR_BYELLOW;
				if (dbm >= 86 && dbm <= 99) rssi_color = COLOR_BRED;
			} else {
				rssi_color = RESET_COLOR;
			}
			
			if (wps_signal_progess_idx < 0) wps_signal_progess_idx = 0;
			if (wps_signal_progess_idx > dbm_array_max_idx) wps_signal_progess_idx = dbm_array_max_idx;
			
			char* signal_dbm = wps_signal_progess[wps_signal_progess_idx];

			if(target != NULL && channel_changed == 0)
			{
				/* Stop the timer. */
				struct itimerspec its = {0};
				timer_settime(globule->timer_id, 0, &its, NULL);
				change_channel(channel);
				channel_changed = 1;
			}

			if(is_probe_resp || is_beacon) {
				wps_parsed = parse_wps_parameters(packet, header->len, wps);
				if(is_beacon || !get_ap_vendor(bssid)) set_ap_vendor(bssid);
			}
			if((get_channel() == channel || source == PCAP_FILE) && !is_done(bssid, wps_parsed ? wps : 0))
			{
				if(is_beacon && 
				   mode == SCAN && 
				   !passive && 
				   should_probe(bssid))
				{
					send_probe_request(get_bssid(), get_ssid());
					probe_sent = 1;
				}
		
				if(!json_mode && ((wps_active(wps) || show_all_aps == 1) && !was_printed(bssid)))
				{
					if(wps_active(wps)) switch(wps->locked)
					{
						case WPSLOCKED:
							lock_display = YES;
							break;
						case UNLOCKED:
						case UNSPECIFIED:
							lock_display = NO;
							break;
					} else lock_display = NO;

					if (show_crack_progress) {
						crack_progress = get_crack_progress(frame_header->addr3);
					}

					char* vendor = get_vendor_string(get_ap_vendor(bssid));
					char* sane_ssid = sanitize_string(ssid);

					if(show_utf8_ssid && verifyssid(ssid))
						strcpy(sane_ssid,ssid);

					if (dbm <= rssi_min) {
					if(wps_active(wps))
					{
						char wps_version[8];
						snprintf(wps_version, sizeof(wps_version), "%d.%d", (wps->version >> 4), (wps->version & 0x0F));
						if (is_pbc(wps)) {
							strcpy(wps_version, "PBC");
						}
						if (!show_no_lck) {
							if (show_crack_progress)
								fprintf(stdout, "%17s  %3d  %.2d  %s  %3s  %8s  %5s  %s\n", bssid, channel, rssi, wps_version, lock_display, vendor ? vendor : "        ", crack_progress ? crack_progress : "-", sane_ssid);
							else {
								if (use_colors) {
									fprintf(stdout, "%s%17s%s ", BSSID_COLOR, bssid, RESET_COLOR);
									fprintf(stdout, "%s%3d%s  ", CHANNEL_COLOR, channel, RESET_COLOR);
									fprintf(stdout, "%s%20s%s ", rssi_color, signal_dbm, RESET_COLOR);
									fprintf(stdout, "%s%.2d%s  ", RSSI_COLOR, rssi, RESET_COLOR);
									fprintf(stdout, "%s%3s%s  ", WPS_VER_COLOR, wps_version, RESET_COLOR);
									fprintf(stdout, "%s%s%s ", WPS_LCK_COLOR, lock_display, RESET_COLOR);
									fprintf(stdout, "%s%8s%s  ", VENDOR_COLOR, vendor ? vendor : "        ", RESET_COLOR);
									fprintf(stdout, "%s%s%s", ESSID_COLOR, sane_ssid, RESET_COLOR);								
									fprintf(stdout, "\n");
								} else {
									fprintf(stdout, "%17s  %3d %20s %.2d  %s  %3s  %8s  %s\n", bssid, channel, signal_dbm, rssi, wps_version, lock_display, vendor ? vendor : "        ", sane_ssid);
								}
							}
						} else {
							// no lck
							if (!lck_wps) {
								if (show_crack_progress)
									fprintf(stdout, "%17s  %3d  %.2d  %s  %8s  %5s  %s\n", bssid, channel, rssi, wps_version, vendor ? vendor : "        ", crack_progress ? crack_progress : "-", sane_ssid);
								else {
									if (use_colors) {
										fprintf(stdout, "%s%17s%s ", BSSID_COLOR, bssid, RESET_COLOR);
										fprintf(stdout, "%s%3d%s  ", CHANNEL_COLOR, channel, RESET_COLOR);
										fprintf(stdout, "%s%20s%s ", rssi_color, signal_dbm, RESET_COLOR);
										fprintf(stdout, "%s%.2d%s  ", RSSI_COLOR, rssi, RESET_COLOR);
										fprintf(stdout, "%s%3s%s  ", WPS_VER_COLOR, wps_version, RESET_COLOR);
										fprintf(stdout, "%s%8s%s  ", VENDOR_COLOR, vendor ? vendor : "        ", RESET_COLOR);
										fprintf(stdout, "%s%s%s", ESSID_COLOR, sane_ssid, RESET_COLOR);								
										fprintf(stdout, "\n");										
									} else {
										fprintf(stdout, "%17s  %3d %20s %.2d  %s  %8s  %s\n", bssid, channel, signal_dbm, rssi, wps_version, vendor ? vendor : "        ", sane_ssid);
									}
								}
							}
						}
					}
					else
					{
						if (!show_no_lck) {
							if (show_crack_progress)
								fprintf(stdout, "%17s  %3d  %.2d            %8s  %5s  %s\n", bssid, channel, rssi, vendor ? vendor : "        ", crack_progress ? crack_progress : "-", sane_ssid);
							else
								if (use_colors) {
									fprintf(stdout, "%s%17s%s ", BSSID_COLOR, bssid, RESET_COLOR);
									fprintf(stdout, "%s%3d%s  ", CHANNEL_COLOR, channel, RESET_COLOR);
									fprintf(stdout, "%s%20s%s ", rssi_color, signal_dbm, RESET_COLOR);
									fprintf(stdout, "%s%.2d%s       ", RSSI_COLOR, rssi, RESET_COLOR);
									fprintf(stdout, "%s%s%s ", WPS_LCK_COLOR, lock_display, RESET_COLOR);
									fprintf(stdout, "%s%8s%s  ", VENDOR_COLOR, vendor ? vendor : "        ", RESET_COLOR);
									fprintf(stdout, "%s%s%s", ESSID_COLOR, sane_ssid, RESET_COLOR);								
									fprintf(stdout, "\n");									
								} else {
									fprintf(stdout, "%17s  %3d %20s %.2d           %8s  %s\n", bssid, channel, signal_dbm, rssi, vendor ? vendor : "        ", sane_ssid);
								}
						} else {
							
						}
					}
					}
					free(sane_ssid);

					if (crack_progress) free(crack_progress);
				}

				if(probe_sent)
				{
					update_probe_count(bssid);
				}

				/* 
				 * If there was no WPS information, then the AP does not support WPS and we should ignore it from here on.
				 * If this was a probe response, then we've gotten all WPS info we can get from this AP and should ignore it from here on.
				 */
				if(!wps_parsed || is_probe_resp)
				{
					mark_ap_complete(bssid);
					if(json_mode && (show_all_aps || wps_active(wps))) {
						if (show_crack_progress) {
							crack_progress = get_crack_progress(frame_header->addr3);
						}
						char *json_string = wps_data_to_json(bssid, ssid, channel, rssi, get_ap_vendor(bssid), wps, crack_progress);
						fprintf(stdout, "%s\n", json_string);
						fflush(stdout);
						free(json_string);

						if (crack_progress) free(crack_progress);
					}
				}
	
			}
		}

		free(bssid);
		bssid = NULL;
	}

end:
	if(wps) free(wps);
	set_bssid((unsigned char *) NULL_MAC);

	return;
}

/* Does what it says */
void send_probe_request(unsigned char *bssid, char *essid)
{
	const void *probe = NULL;
	size_t probe_size = 0;

	probe = build_wps_probe_request(bssid, essid, &probe_size);
	if(probe)
	{
		send_packet(probe, probe_size, 0);
		free((void *) probe);
	}

	return;
}

/* Whenever a SIGALRM is thrown, go to the next 802.11 channel */
void sigalrm_handler(int x)
{
	next_channel();
}

static void print_header(void) {
	fprintf(stderr, "\nWash v%s WiFi Protected Setup Scan Tool\n", get_version());
        fprintf(stderr, "Copyright (c) 2011, Tactical Network Solutions, Craig Heffner\n\n");
}

static void wash_usage(char *prog)
{
	print_header();

	fprintf(stderr, "Required Arguments:\n");
	fprintf(stderr, "\t-i, --interface=<iface>              Interface to capture packets on\n");
	fprintf(stderr, "\t-f, --file [FILE1 FILE2 FILE3 ...]   Read packets from capture files\n");

	fprintf(stderr, "\nOptional Arguments:\n");
	fprintf(stderr, "\t-c, --channel=<num>                  Channel to listen on [auto]\n");
	fprintf(stderr, "\t-n, --probes=<num>                   Maximum number of probes to send to each AP in scan mode [%d]\n", DEFAULT_MAX_NUM_PROBES);
	fprintf(stderr, "\t-O, --output-file=<filename>         Write packets of interest into pcap file\n");
	fprintf(stderr, "\t-F, --ignore-fcs                     Ignore frame checksum errors\n");
	fprintf(stderr, "\t-2, --2ghz                           Use 2.4GHz 802.11 channels\n");
	fprintf(stderr, "\t-5, --5ghz                           Use 5GHz 802.11 channels\n");
	fprintf(stderr, "\t-s, --scan                           Use scan mode\n");
	fprintf(stderr, "\t-u, --survey                         Use survey mode [default]\n");
	fprintf(stderr, "\t-a, --all                            Show all APs, even those without WPS\n");
	fprintf(stderr, "\t-j, --json                           print extended WPS info as json\n");
	fprintf(stderr, "\t-U, --utf8                           Show UTF8 ESSID (does not sanitize ESSID, dangerous)\n");
	fprintf(stderr, "\t-p, --progress                       Show percentage of crack progress\n");
	fprintf(stderr, "\t-Z, --no-lck                         Do not show locked WPS\n");
	fprintf(stderr, "\t-R, --rssi-min                       Minumum RSSI to show\n");
	fprintf(stderr, "\t-C, --colors                         Use color scheme\n");
	fprintf(stderr, "\t-h, --help                           Show help\n");
	
	fprintf(stderr, "\nExample:\n");
	fprintf(stderr, "\t%s -i wlan0mon\n\n", prog);

	return;
}
