#include <string.h>

char *get_vendor_string(const unsigned char* oui) {
	#define VENDOR_STR_SIZE (8 + 1)
	struct vendor {
		unsigned char id[3];
		char name[VENDOR_STR_SIZE];
	};

	static const struct vendor vendors[] =
	{ /* Using the same names as Wireshark */
		{"\x00\x10\x18", "Broadcom"}, // Broadcom
		{"\x00\x03\x7F", "AtherosC"}, // Atheros Communications
		{"\x00\x13\x74", "AtherosC"}, // Atheros Communications
		{"\x00\x0C\x43", "RalinkTe"}, // Ralink Technology, Corp.
		{"\x00\x17\xA5", "RalinkTe"}, // Ralink Technology, Corp.
		{"\x00\xE0\x4C", "RealtekS"}, // Realtek Semiconductor Corp.
		{"\x00\xA0\x00", "Mediatek"}, // Mediatek Corp.
		{"\x00\x0C\xE7", "Mediatek"}, // Mediatek Corp.
		{"\x00\x1C\x51", "CelenoCo"}, // Celeno Communications
		{"\x00\x50\x43", "MarvellS"}, // Marvell Semiconductor
		{"\x00\x26\x86", "Quantenn"}, // Quantenna Communications
		{"\x00\x09\x86", "LantiqML"}, // Lantiq Microsystems
		{"\xAC\x85\x3D", "HuaweiTe"}, // Huawei Technologies Co., Ltd.
		{"\x00\xE0\xFC", "HuaweiTe"}, // Huawei Technologies Co., Ltd.
		{"\x88\x12\x4E", "Qualcomm"}, // Qualcomm Atheros, Inc.
		{"\x8C\xFD\xF0", "Qualcomm"}, // Qualcomm Atheros, Inc.
		{"\x00\xA0\xCC", "Lite-OnC"}, // Lite-On Communications Inc.
		{"\x40\x45\xDA", "SpreadTe"}, // Spreadtrum Communications Inc.
		{"\x18\xFE\x34", "Espressi"}, // Espressif Inc.
		{"\x50\xFF\x20", "Keenetic"}, // Keenetic International Ltd.
		{"\x00\x0F\x66", "AzureWav"}, // AzureWave Technology Inc.
		{"\x00\x0E\x2E", "AzureWav"}, // AzureWave Technology Inc.
		{"\x00\x0E\x2F", "AzureWav"}, // AzureWave Technology Inc.
		{"\x00\x0E\x2D", "AzureWav"}, // AzureWave Technology Inc.
		{"\x00\x0E\x2C", "AzureWav"}, // AzureWave Technology Inc.
		{"\x00\x1D\x0F", "Tp-LinkT"}, // Tp-Link Technologies Co., Ltd.
		{"\x00\x50\xF2", "Microsof"}  // Microsoft
	};

	#define VENDOR_LIST_SIZE (sizeof(vendors)/sizeof(vendors[0]))

	if(!oui) return 0;

	int i;
	for (i = 0; i < VENDOR_LIST_SIZE; i++)
		if (!memcmp(oui, vendors[i].id, 3))
			return (void*) vendors[i].name;

	return "Unknown ";

}
