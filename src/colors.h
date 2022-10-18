#ifndef COLORS_H
#define COLORS_H

// Reset colors
#define RESET_COLOR		"\e[0m"

// General colors
#define COLOR_BLACK		"\e[0;30m"
#define COLOR_RED		"\e[0;31m"
#define COLOR_GREEN		"\e[0;32m"
#define COLOR_YELLOW	"\e[0;33m"
#define COLOR_BLUE		"\e[0;34m"
#define COLOR_PURPLE	"\e[0;35m"
#define COLOR_CYAN		"\e[0;36m"
#define COLOR_WHITE		"\e[0;36m"

// Bold
#define COLOR_BBLACK	"\e[1;30m"
#define COLOR_BRED		"\e[1;31m"
#define COLOR_BGREEN	"\e[1;32m"
#define COLOR_BYELLOW	"\e[1;33m"
#define COLOR_BBLUE		"\e[1;34m"
#define COLOR_BPURPLE	"\e[1;35m"
#define COLOR_BCYAN		"\e[1;36m"
#define COLOR_BWHITE	"\e[1;36m"

// Underline
#define COLOR_UBLACK	"\e[4;30m"
#define COLOR_URED		"\e[4;31m"
#define COLOR_UGREEN	"\e[4;32m"
#define COLOR_UYELLOW	"\e[4;33m"
#define COLOR_UBLUE		"\e[4;34m"
#define COLOR_UPURPLE	"\e[4;35m"
#define COLOR_UCYAN		"\e[4;36m"
#define COLOR_UWHITE	"\e[4;36m"


// Color scheme
#define HEAD_COLOR		COLOR_BGREEN
#define LINE_COLOR		COLOR_PURPLE
#define BSSID_COLOR		COLOR_BPURPLE
#define CHANNEL_COLOR	COLOR_BWHITE
#define RSSI_COLOR		COLOR_BCYAN
#define WPS_VER_COLOR	COLOR_BWHITE
#define WPS_LCK_COLOR	COLOR_BYELLOW
#define VENDOR_COLOR	COLOR_BBLUE
#define ESSID_COLOR		COLOR_BRED

#endif // COLORS_H
