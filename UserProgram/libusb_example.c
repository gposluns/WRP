#include <libusb.h>
#include <stdio.h>

#define FMT_ERROR_LD "error=%ld\n"
#define PRINT_ERR(r) printf("error=%s\n", libusb_error_name(r))

#define LUFA_VENDOR_ID 1003
#define LUFA_PRODUCT_ID 8261

//#define DEBUG 1

void print_dev(libusb_device *dev, struct libusb_device_descriptor * desc, int i) {
	int r = libusb_get_device_descriptor(dev, desc);
	if (r < 0)  {
		PRINT_ERR(r);
		return;
	}

	#ifdef DEBUG
	printf("** device %d **\n", i);
	printf("num of possible configurations: %d\n", (int)desc->bNumConfigurations);
	printf("device class: %d\n", (int)desc->bDeviceClass);
	printf("vendor id: %d\n", desc->idVendor);
	printf("product id: %d\n", desc->idProduct);
	printf("device address: %d\n", libusb_get_device_address(dev));
	#endif

	struct libusb_config_descriptor *config;
	libusb_get_config_descriptor(dev, 0, &config);

	#ifdef DEBUG
	printf("interfaces: %d\n", (int)config->bNumInterfaces);
	#endif

	const struct libusb_interface *inter;
	const struct libusb_interface_descriptor *interdesc;
	const struct libusb_endpoint_descriptor *epdesc;

	for(int i=0; i<(int)config->bNumInterfaces; i++) {
		inter = &config->interface[i];
		#ifdef DEBUG
		printf("num of alternate settings: %d\n", inter->num_altsetting);
		#endif

		for(int j=0; j<inter->num_altsetting; j++) {
			interdesc = &inter->altsetting[j];
			#ifdef DEBUG
			printf("interface number: %d\n", (int)interdesc->bInterfaceNumber);
			printf("num of endpoints: %d\n", (int)interdesc->bNumEndpoints);
			#endif

			for(int k=0; k<(int)interdesc->bNumEndpoints; k++) {
				epdesc = &interdesc->endpoint[k];
				#ifdef DEBUG
				printf("descriptor type: %d\n", (int)epdesc->bDescriptorType);
				printf("endpoint address: %d\n", (int)epdesc->bEndpointAddress);
				#endif
			}
		}
	}

	libusb_free_config_descriptor(config);
	return;
}

int main (int argc, char **argv) {
	int r;

	libusb_device **devs; // list of devices
	libusb_context *ctx = NULL; // libusb session
	ssize_t cnt; // number of devices

	r = libusb_init(&ctx);
	if (r < 0) {
		PRINT_ERR(r);
		return 1;
	}

 	libusb_set_option(ctx, 3); // set verbosity level
 	cnt = libusb_get_device_list(ctx, &devs);
 	if (cnt < 0) {
 		printf(FMT_ERROR_LD, cnt);
 	}
 	printf("number of devices=%ld\n", cnt);

	struct libusb_device_descriptor desc;
	libusb_device *dev;

 	for(ssize_t i = 0; i < cnt; i++) {
 		print_dev(devs[i], &desc, i);
		if (desc.idVendor == LUFA_VENDOR_ID && desc.idProduct == LUFA_PRODUCT_ID) {
 			dev = devs[i];
 			break;
 		}
 	}

	// handle device
    struct libusb_device_handle *dh = NULL;
    r = libusb_open(dev, &dh);
	if (r < 0) {
		PRINT_ERR(r);
		return 1;
	}

    if (!dh) {
        printf("error: cannot connect to device %d\n", libusb_get_device_address(dev));
    }

 	libusb_free_device_list(devs, 1); // free device list
 	libusb_exit(ctx); // free ctx
 	return 0;
}