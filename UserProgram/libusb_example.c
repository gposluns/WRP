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

 	//libusb_set_option(ctx, 3); // set verbosity level
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
	} else {
		printf("device opened!\n");
	}

    if (!dh) {
        printf("error: cannot connect to device %d\n", libusb_get_device_address(dev));
    }

    char data[4];

    data[0]='a';data[1]='b';data[2]='c';data[3]='d';

    int actual; //used to find out how many bytes were written
    if(libusb_kernel_driver_active(dh, 0) == 1) { //find out if kernel driver is attached
    	printf("kernel driver active\n");
    	if(libusb_detach_kernel_driver(dh, 0) == 0) //detach it
    		printf("kernel driver detached\n");
    }

    r = libusb_claim_interface(dh, 0); //claim interface 0 (the first) of device
	if (r < 0) {
		PRINT_ERR(r);
		return 1;
	} else {
		printf("interface claimed!\n");
	}

	// TODO: find read/write endpoints
	r = libusb_bulk_transfer(dh, (2 | LIBUSB_ENDPOINT_OUT), data, 4, &actual, 0);
	if (r < 0) {
		PRINT_ERR(r);
		return 1;
	} else {
		printf("write successful!\n");
	}	

	r = libusb_release_interface(dh, 0); 
	if (r < 0) {
		PRINT_ERR(r);
		return 1;
	} else {
		printf("released interface\n");
	}	

	libusb_close(dh);
 	libusb_free_device_list(devs, 1); // free device list
 	libusb_exit(ctx); // free ctx
 	return 0;
}