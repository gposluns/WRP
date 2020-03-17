sources := libusb_example.c wrp_mv.c 
targets := libusb_example wrp_mv 

default: all
all: $(targets)

wrp_mv : wrp_mv.c
	gcc -o wrp_mv wrp_mv.c

libusb_example : libusb_example.c
	gcc -o libusb_example libusb_example.c /usr/local/lib/libusb-1.0.so

clean:
	rm wrp_mv libusb_example