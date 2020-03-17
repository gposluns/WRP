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

#define SCSI_CMD_INQUIRY                               0x12
#define SCSI_CMD_REQUEST_SENSE                         0x03
#define SCSI_CMD_TEST_UNIT_READY                       0x00
#define SCSI_CMD_READ_CAPACITY_10                      0x25
#define SCSI_CMD_SEND_DIAGNOSTIC                       0x1D
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL          0x1E
#define SCSI_CMD_WRITE_10                              0x2A
#define SCSI_CMD_READ_10                               0x28

/** Mask for a Command Block Wrapper's flags attribute to specify a command with data sent from host-to-device. */
#define COMMAND_DIRECTION_DATA_OUT (0 << 7)

/** Mask for a Command Block Wrapper's flags attribute to specify a command with data sent from device-to-host. */
#define COMMAND_DIRECTION_DATA_IN  (1 << 7)

#define ERR_EXIT(errcode) do { perr("   %s\n", libusb_strerror((enum libusb_error)errcode)); return -1; } while (0)
#define CALL_CHECK(fcall) do { r=fcall; if (r < 0) ERR_EXIT(r); } while (0);

// Section 5.1: Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t Signature[4];
	uint32_t Tag;
	uint32_t DataTransferLength;
	uint8_t Flags;
	uint8_t LUN;
	uint8_t SCSICommandLength;
	uint8_t SCSICommandData[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t Signature[4];
	uint32_t Tag;
	uint32_t DataResidue;
	uint8_t Status;
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
	if (csw.Tag != expected_tag) {
		perr("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw.Tag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printf("   Mass Storage Status: %02X (%s)\n", csw.Status, csw.Status?"FAILED":"Success");
	if (csw.Tag != expected_tag)
		return -1;
	if (csw.Status) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw.Status == 1) {
			printf("CSW DataResidue:%d\n", csw.DataResidue);
			return -2;	// request Get Sense
		}
		else
			return -1;
	}

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

	if (direction == COMMAND_DIRECTION_DATA_OUT) cdb_len = 9;
	if (direction == COMMAND_DIRECTION_DATA_IN) cdb_len = 9;

	memset(&cbw, 0, sizeof(cbw));
	cbw.Signature[0] = 'U';
	cbw.Signature[1] = 'S';
	cbw.Signature[2] = 'B';
	cbw.Signature[3] = 'C';
	*ret_tag = tag;
	cbw.Tag = tag++;
	cbw.DataTransferLength = data_length;
	cbw.Flags = direction;
	cbw.LUN = 0;
	cbw.SCSICommandLength = cdb_len;
	memcpy(cbw.SCSICommandData, cdb, cdb_len);

	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&cbw, cdb_len, &size, 1000);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (r != LIBUSB_SUCCESS) {
		perr("   send_mass_storage_command: %s\n", libusb_strerror((enum libusb_error)r));
		return -1;
	}

	printf("   sent %d command bytes\n", cdb_len);
	return 0;
}

int write_and_get_status(libusb_device_handle *dh, uint8_t endpoint_out, uint8_t endpoint_in, uint8_t *command, int data_length, uint8_t *data, uint32_t *tag) {
	int r = send_mass_storage_command(dh, endpoint_out, command, COMMAND_DIRECTION_DATA_OUT, data_length, tag);
	if (r < 0) {
		printf("cannot write data\n");
		return r;
	} 

	int size;
	CALL_CHECK(libusb_bulk_transfer(dh, endpoint_out, (unsigned char *)&data, data_length, &size, 5000));
	if (size != data_length) {
		printf("could not write all data\n");
		return -1;
	}

	r = get_mass_storage_status(dh, endpoint_in, *tag);
	if (r < 0) {
		printf("cannot get write status\n");
		return r;
	}
	return 0;
}

int read_and_get_status(libusb_device_handle *dh, uint8_t endpoint_out, uint8_t endpoint_in, uint8_t *command, int data_length, uint8_t *data, uint32_t *tag) {
	int r = send_mass_storage_command(dh, endpoint_out, command, COMMAND_DIRECTION_DATA_IN, data_length, tag);
	if (r < 0) {
		printf("cannot read data\n");
		return r;
	} 

	char buffer[512];
	int size, i;
	CALL_CHECK(libusb_bulk_transfer(dh, endpoint_in, (unsigned char*)&buffer, data_length, &size, 1000));
	memcpy(data, buffer, size);
	printf("size=%d data=%s\n", size, data);

	r = get_mass_storage_status(dh, endpoint_in, *tag);
	if (r < 0) {
		printf("cannot get read status\n");
		return r;
	}
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

    int data_length = 1;
    int command_length = 9;

    char * write_command = (char *)malloc(sizeof(char *)*command_length);
    char * read_command = (char *)malloc(sizeof(char *)*command_length);

    int totalBlocks = 1;
    int blockAddress = 0;

    unsigned char * data = (unsigned char *)malloc(sizeof(unsigned char *)*data_length);
    strcpy(data, "nootwashere");

    write_command[0] = SCSI_CMD_WRITE_10;
   	memcpy(write_command+2, &blockAddress, 4);
   	memcpy(write_command+7, &totalBlocks, 2);

   	read_command[0] = SCSI_CMD_READ_10;
   	memcpy(read_command+2, &blockAddress, 4);
   	memcpy(read_command+7, &totalBlocks, 2);

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

	int endpoint_out = 4;
	int endpoint_in = 131;
	uint32_t tag;

	r = write_and_get_status(dh, endpoint_out, endpoint_in, write_command, data_length, data, &tag);
	if (r < 0) {
		return 1;
	} else {
		printf("write command sent successfully!\n");
	}

	r = read_and_get_status(dh, endpoint_out, endpoint_in, read_command, data_length, data, &tag);
	if (r < 0) {
		return 1;
	} else {
		printf("read command sent successfully!\n");
	}

	r = libusb_release_interface(dh, 0); 
	if (r < 0) {
		PRINT_ERR("cannot release interface", r);
		return 1;
	} else {
		printf("released interface\n");
	}	

	free(read_command);
	free(write_command);
	free(data);
	libusb_close(dh);
 	libusb_free_device_list(devs, 1); // free device list
 	libusb_exit(ctx); // free ctx
 	return 0;
}