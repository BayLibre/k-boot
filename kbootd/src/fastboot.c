// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usb/functionfs.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#define MTK_USB_DRIVER          "11201000.usb"

#define FASTBOOT_INTERFACE_NAME "kbootd"

#define USB_GADGET_UDC          "/config/usb_gadget/g1/UDC"

#define USB_FASTBOOT_EP0        "/dev/usb-ffs/fastboot/ep0"
#define USB_FASTBOOT_OUT        "/dev/usb-ffs/fastboot/ep1"
#define USB_FASTBOOT_IN         "/dev/usb-ffs/fastboot/ep2"

/* TODO: why this max value ? */
#define FASTBOOT_READ_COUNT     (4096 * 15)

#define MAX_PACKET_SIZE_FS      64
#define MAX_PACKET_SIZE_HS      512
#define MAX_PACKET_SIZE_SS      1024

struct FuncDesc {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_endpoint_descriptor_no_audio sink;
} __attribute__((packed));

struct SsFuncDesc {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio source;
        struct usb_ss_ep_comp_descriptor source_comp;
        struct usb_endpoint_descriptor_no_audio sink;
        struct usb_ss_ep_comp_descriptor sink_comp;
} __attribute__((packed));

struct DescV2 {
        struct usb_functionfs_descs_head_v2 header;
        /* The rest of the structure depends on the flags in the header. */
        __le32 fs_count;
        __le32 hs_count;
        __le32 ss_count;
        struct FuncDesc fs_descs, hs_descs;
        struct SsFuncDesc ss_descs;
} __attribute__((packed));

const struct usb_interface_descriptor fastboot_interface = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceSubClass = 66,
        .bInterfaceProtocol = 3,
        .iInterface = 1, /* first string from the provided table */
};

static const struct FuncDesc fs_descriptors = {
        .intf = fastboot_interface,
        .source =
	{
		.bLength = sizeof(fs_descriptors.source),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_OUT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_FS,
	},
        .sink =
	{
		.bLength = sizeof(fs_descriptors.sink),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_FS,
	},
};

static const struct FuncDesc hs_descriptors = {
        .intf = fastboot_interface,
        .source =
	{
		.bLength = sizeof(hs_descriptors.source),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_OUT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_HS,
	},
        .sink =
	{
		.bLength = sizeof(hs_descriptors.sink),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_HS,
	},
};

static const struct SsFuncDesc ss_descriptors = {
        .intf = fastboot_interface,
        .source =
	{
		.bLength = sizeof(ss_descriptors.source),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_OUT,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_SS,
	},
        .source_comp =
	{
		.bLength = sizeof(ss_descriptors.source_comp),
		.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
		.bMaxBurst = 15,
	},
        .sink =
	{
		.bLength = sizeof(ss_descriptors.sink),
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = 1 | USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = MAX_PACKET_SIZE_SS,
	},
        .sink_comp =
	{
		.bLength = sizeof(ss_descriptors.sink_comp),
		.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
		.bMaxBurst = 15,
	},
};

static const struct {
        struct usb_functionfs_strings_head header;
        struct {
                __le16 code;
                const char str1[sizeof(FASTBOOT_INTERFACE_NAME)];
        } __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
        .header =
	{
		.magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
		.length = htole32(sizeof(strings)),
		.str_count = htole32(1),
		.lang_count = htole32(1),
	},
        .lang0 =
	{
		htole16(0x0409), /* en-us */
		FASTBOOT_INTERFACE_NAME,
	},
};

static struct DescV2 v2_descriptor = {
        .header =
	{
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.length = htole32(sizeof(v2_descriptor)),
		.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
		FUNCTIONFS_HAS_SS_DESC,
	},
        .fs_count = 3,
        .hs_count = 3,
        .ss_count = 5,
        .fs_descs = fs_descriptors,
        .hs_descs = hs_descriptors,
        .ss_descs = ss_descriptors,
};

static int fb_ep0;
static int fb_in;
static int fb_out;

int fastboot_write(char *buffer, size_t buffer_count)
{
        return kwrite(fb_in, buffer, buffer_count);
}

int fastboot_read(char *buffer, size_t buffer_count)
{
        return read(fb_out, buffer, buffer_count);
}

int fastboot_read_full(char *buffer, size_t buffer_count)
{
        return kread_full(fb_out, buffer, buffer_count, FASTBOOT_READ_COUNT);
}

int fastboot_init(void)
{
        int ret;

        run_program("setup_fastboot", false);

        fb_ep0 = open(USB_FASTBOOT_EP0, O_RDWR);
        if (fb_ep0 == -1) {
                log("open %s failed: %s\n", USB_FASTBOOT_EP0, strerror(errno));
                return -1;
        }

        ret = kwrite(fb_ep0, (char *)&v2_descriptor, sizeof(v2_descriptor));
        if (ret == -1) {
                log("write usb descriptors failed\n");
                return -1;
        }

        ret = kwrite(fb_ep0, (char *)&strings, sizeof(strings));
        if (ret == -1) {
                log("write usb strings failed\n");
                return -1;
        }

        ret = write_to_file(USB_GADGET_UDC, MTK_USB_DRIVER, 12);
        if (ret < 0) {
                log("write usb gadget failed\n");
                return -1;
        }

        fb_in = open(USB_FASTBOOT_IN, O_WRONLY);
        if (fb_in == -1) {
                log("open %s failed: %s\n", USB_FASTBOOT_IN, strerror(errno));
                return -1;
        }

        fb_out = open(USB_FASTBOOT_OUT, O_RDONLY);
        if (fb_out == -1) {
                log("open %s failed: %s\n", USB_FASTBOOT_OUT, strerror(errno));
                return -1;
        }

        return 0;
}
