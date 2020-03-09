#include <libusb.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define FMT_ERROR_LD "error=%ld\n"
#define PRINT_ERR(msg, r) printf("%s: error=%s\n", msg, libusb_error_name(r))

#define LUFA_VENDOR_ID 1003
#define LUFA_PRODUCT_ID 8261

//#define DEBUG 1

#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24

#define SCSI_CMD_WRITE_10                              0x2A
#define SCSI_CMD_READ_10                               0x28

/** Mask for a Command Block Wrapper's flags attribute to specify a command with data sent from host-to-device. */
#define COMMAND_DIRECTION_DATA_OUT (0 << 7)

/** Mask for a Command Block Wrapper's flags attribute to specify a command with data sent from device-to-host. */
#define COMMAND_DIRECTION_DATA_IN  (1 << 7)


// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,02,00,  //  F
};

static int perr(char const *format, ...)
{
	va_list args;
	int r;

	va_start (args, format);
	r = vfprintf(stderr, format, args);
	va_end(args);

	return r;
}

void print_dev(libusb_device *dev, struct libusb_device_descriptor * desc, int i) {
	int r = libusb_get_device_descriptor(dev, desc);
	if (r < 0)  {
		PRINT_ERR("cannot get device descriptor", r);
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

static int get_mass_storage_status(libusb_device_handle *handle, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper csw;

	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&csw, 13, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr("   get_mass_storage_status: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}
	if (size != 13) {
		perr("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw.dCSWTag != expected_tag) {
		perr("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw.dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printf("   Mass Storage Status: %02X (%s)\n", csw.bCSWStatus, csw.bCSWStatus?"FAILED":"Success");
	if (csw.dCSWTag != expected_tag)
		return -1;
	if (csw.bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw.bCSWStatus == 1) {
			printf("CSW DataResidue:%d\n", csw.dCSWDataResidue);
			return -2;	// request Get Sense
		}
		else
			return -1;
	}

	// do {
	// 	uint8_t b;
	// 	r = libusb_bulk_transfer(handle, endpoint, &b, 1, &size, 1000);
	// 	// if (r == LIBUSB_ERROR_PIPE) {
	// 	// 	libusb_clear_halt(handle, endpoint);
	// 	// }
	// 	// i++;
	// } while ((r == LIBUSB_ERROR_PIPE));

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}

static int send_mass_storage_command(libusb_device_handle *handle, uint8_t endpoint, 
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size;
	struct command_block_wrapper cbw;

	if (cdb == NULL) {
		return -1;
	}

	if (endpoint & LIBUSB_ENDPOINT_IN) {
		perr("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw.CBWCB))) {
		perr("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}
	// if (cdb[0] == 0xFE) /* vendor */
	// 	if (cdb[1] == 2) /* special command length*/
	// 		cdb_len = 6;

	memset(&cbw, 0, sizeof(cbw));
	cbw.dCBWSignature[0] = 'U';
	cbw.dCBWSignature[1] = 'S';
	cbw.dCBWSignature[2] = 'B';
	cbw.dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw.dCBWTag = tag++;
	cbw.dCBWDataTransferLength = data_length;
	cbw.bmCBWFlags = direction;
	cbw.bCBWLUN = 0;
	// Subclass is 1 or 6 => cdb_len
	cbw.bCBWCBLength = cdb_len;
	memcpy(cbw.CBWCB, cdb, cdb_len);

	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&cbw, 31, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr("   send_mass_storage_command: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}

	printf("   sent %d CDB bytes\n", cdb_len);
	return 0;
}

int main (int argc, char **argv) {
	int r;

	libusb_device **devs; // list of devices
	libusb_context *ctx = NULL; // libusb session
	ssize_t cnt; // number of devices

	r = libusb_init(&ctx);
	if (r < 0) {
		PRINT_ERR("cannot init libusb", r);
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
		PRINT_ERR("cannot open device", r);
		return 1;
	} else {
		printf("device opened!\n");
	}

    if (!dh) {
        printf("error: cannot connect to device %d\n", libusb_get_device_address(dev));
    }

    char * writeData = (char *)malloc(sizeof(char *)*(512+10));
    char * readData = (char *)malloc(sizeof(char *)*(512+10));

    writeData[0]=SCSI_CMD_WRITE_10;
    int totalBlocks = 1;
    int blockAddress = 0x00000000;
    char * data = (char *)malloc(sizeof(char *)*512);
    strcpy(data, "noot");

   	memcpy(writeData+2, &blockAddress, 4);
   	memcpy(writeData+7, &totalBlocks, 2);
   	memcpy(writeData+9, data, 512);

   	readData[0] = SCSI_CMD_READ_10;
   	memcpy(readData+2, &blockAddress, 4);
   	memcpy(readData+7, &totalBlocks, 2);

    //int actual; //used to find out how many bytes were written
    if(libusb_kernel_driver_active(dh, 0) == 1) { //find out if kernel driver is attached
    	printf("kernel driver active\n");
    	if(libusb_detach_kernel_driver(dh, 0) == 0) //detach it
    		printf("kernel driver detached\n");
    }

    r = libusb_claim_interface(dh, 0); //claim interface 0 (the first) of device
	if (r < 0) {
		PRINT_ERR("cannot claim interface", r);
		return 1;
	} else {
		printf("interface claimed!\n");
	}

	// endpoints: 4 = write and 131 = read ?
	int endpoint_out = 4;
	int endpoint_in = 131;
	uint32_t expected_tag;

	r = send_mass_storage_command(dh, endpoint_out, writeData, COMMAND_DIRECTION_DATA_OUT, 522, &expected_tag);
	if (r < 0) {
		printf("cannot write data\n");
		return 1;
	} else {
		printf("write successful!\n");
	}

	r = get_mass_storage_status(dh, endpoint_in, expected_tag);
	if (r < 0) {
		return 1;
	} else {
		printf("got status!\n");
	}

	r = send_mass_storage_command(dh, endpoint_out, readData, COMMAND_DIRECTION_DATA_IN, 522, &expected_tag);
	if (r < 0) {
		PRINT_ERR("cannot read data", r);
		return 1;
	} else {
		printf("read successful!\n");
	 	printf("readData: %s\n", readData);
	}

	r = get_mass_storage_status(dh, endpoint_in, expected_tag);
	if (r < 0) {
		return 1;
	} else {
		printf("got status!\n");
	}

	r = libusb_release_interface(dh, 0); 
	if (r < 0) {
		PRINT_ERR("cannot release interface", r);
		return 1;
	} else {
		printf("released interface\n");
	}	

	free(readData);
	free(writeData);
	free(data);
	libusb_close(dh);
 	libusb_free_device_list(devs, 1); // free device list
 	libusb_exit(ctx); // free ctx
 	return 0;
}