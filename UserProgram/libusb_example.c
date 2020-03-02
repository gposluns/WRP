#include <libusb.h>
#include <stdio.h>

#define FMT_ERROR "error=%d\n"
#define FMT_ERROR_LD "error=%ld\n"

void print_dev(libusb_device *dev, int i) {
	struct libusb_device_descriptor desc;
	int r = libusb_get_device_descriptor(dev, &desc);
	if (r < 0)  {
		printf(FMT_ERROR, r);
		return;
	}

	// LUFA vendor id
	if (desc.idVendor != 1003) return;

	printf("** device %d **\n", i);
	printf("num of possible configurations: %d\n", (int)desc.bNumConfigurations);
	printf("device class: %d\n", (int)desc.bDeviceClass);
	printf("vendor id: %d\n", desc.idVendor);
	printf("product id: %d\n", desc.idProduct);
	printf("device address: %d\n", libusb_get_device_address(dev));

	struct libusb_config_descriptor *config;
	libusb_get_config_descriptor(dev, 0, &config);

	printf("interfaces: %d\n", (int)config->bNumInterfaces);

	const struct libusb_interface *inter;
	const struct libusb_interface_descriptor *interdesc;
	const struct libusb_endpoint_descriptor *epdesc;

	for(int i=0; i<(int)config->bNumInterfaces; i++) {
		inter = &config->interface[i];
		printf("num of alternate settings: %d\n", inter->num_altsetting);
		for(int j=0; j<inter->num_altsetting; j++) {
			interdesc = &inter->altsetting[j];
			printf("interface number: %d\n", (int)interdesc->bInterfaceNumber);
			printf("num of endpoints: %d\n", (int)interdesc->bNumEndpoints);
			for(int k=0; k<(int)interdesc->bNumEndpoints; k++) {
				epdesc = &interdesc->endpoint[k];
				printf("descriptor type: %d\n", (int)epdesc->bDescriptorType);
				printf("endpoint address: %d\n", (int)epdesc->bEndpointAddress);
			}
		}
	}

	libusb_free_config_descriptor(config);
}

int main (int argc, char **argv) {
	int r;

	libusb_device **devs; // list of devices
	libusb_context *ctx = NULL; // libusb session
	ssize_t cnt; // number of devices

	r = libusb_init(&ctx);
	if (r < 0) {
		printf(FMT_ERROR, r);
		return 1;
	}

 	libusb_set_option(ctx, 3); // set verbosity level
 	cnt = libusb_get_device_list(ctx, &devs);
 	if (cnt < 0) {
 		printf(FMT_ERROR_LD, cnt);
 	}
 	printf("number of devices=%ld\n", cnt);

 	for(ssize_t i = 0; i < cnt; i++) {
 		print_dev(devs[i], i);
 	}

 	libusb_free_device_list(devs, 1); // free device list
 	libusb_exit(ctx); // free ctx
 	return 0;
}