/* This program is distributed under the GPL, version 2 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <ftdi.h>
#include <assert.h>
#include <sched.h>
#include <error.h>
#include <errno.h>

const unsigned data_rate = 1000000;	// Hz

static void write_bit(struct ftdi_context *ftdi, bool bit)
{
	uint8_t data = bit ? 0xff : 0;
	int f = ftdi_write_data(ftdi, &data, 1);
	if (f < 0) {
		fprintf(stderr, "write failed, error %d (%s)\n", f, ftdi_get_error_string(ftdi));
		exit(1);
	}
}

static bool read_bit(uint8_t **bufptr)
{
	bool rv = !!(**bufptr & INVERT_RXD);
	(*bufptr)++;
	putchar(rv ? '1' : '0');
	return rv;
}

static void read_data(struct ftdi_context *ftdi, uint8_t *buf, unsigned len)
{
	int f = ftdi_read_data(ftdi, buf, len);
	if (f < 0) {
		fprintf(stderr, "read failed, error %d (%s)\n", f, ftdi_get_error_string(ftdi));
		exit(1);
	}
}

static int wait_bit(uint8_t **bufptr, bool expected_bit)
{
	bool current_bit = !expected_bit;
	unsigned period = 0;
	while (current_bit != expected_bit) {
		current_bit = read_bit(bufptr);
		period++;
		if (period > 1000) {
			return -1;
		}
	}
	return (int)((uint64_t)period * 1000000 / data_rate);
}

static bool wait_begin(uint8_t **bufptr)
{
	int period1 = wait_bit(bufptr, false);
	if (period1 < 0) {
		return false;
	}
	int period2 = wait_bit(bufptr, true);
	if (period2 < 0) {
		return false;
	}
	int period3 = wait_bit(bufptr, false);
	if (period3 < 0) {
		return false;
	}
	printf("It took the device %uµs to answer (expected: 170 to 370)\n", period1 + period2 + period3);
	return true;
}

static int read_protocol_bit(uint8_t **bufptr)
{
	int down_period = wait_bit(bufptr, true);
	int up_period = wait_bit(bufptr, false);
	if (down_period < 0 || up_period < 0) {
		return -1;
	}
	bool rv = up_period * 3 / 2 > down_period;
	printf("down: %u, up: %u, result: %s\n", down_period, up_period, rv ? "yea" : "no");
	return rv;
}

int main(int argc, char **argv)
{
	struct ftdi_context *ftdi;
	int f;
	int retval = 0;
	//uint8_t bits[10000ULL * data_rate / 1000000];
	uint8_t bits[4096 * 3];

	if ((ftdi = ftdi_new()) == 0) {
		fprintf(stderr, "ftdi_new failed\n");
		return EXIT_FAILURE;
	}

	f = ftdi_usb_open(ftdi, 0x0403, 0x6001);
	//f = ftdi_usb_open(ftdi, 0x0403, 0x6010);
	//f = ftdi_usb_open(ftdi, 0x0403, 0x6014);

	if (f < 0 && f != -5) {
		fprintf(stderr, "unable to open ftdi device: %d (%s)\n", f, ftdi_get_error_string(ftdi));
		retval = 1;
		goto done;
	}

	// INVERT means "use as output" ;)
	unsigned output_port = INVERT_TXD;
	ftdi_set_bitmode(ftdi, output_port, BITMODE_BITBANG);
	ftdi_set_baudrate(ftdi, data_rate / 30);
	//ftdi_read_data_set_chunksize(ftdi, sizeof(bits) / 8);

	struct sched_param param;
	param.sched_priority = 50;
	if (sched_setscheduler(0, SCHED_FIFO, &param)) {
		error(0, errno, "couldn't set SCHED_FIFO: ");
	}

	while (1) {
		uint8_t *bits_ptr = bits;
		ftdi_usb_purge_tx_buffer(ftdi);
		write_bit(ftdi, 0);
		usleep(1000);
		write_bit(ftdi, 1);
		ftdi_usb_purge_rx_buffer(ftdi);
		read_data(ftdi, bits, sizeof(bits));
		printf("\n%lu\n", sizeof(bits));
		/*
		for (int i = 0; i < sizeof(bits); ++i) {
			putchar(bits[i] & INVERT_RXD ? '1' : '0');
		}
		*/
		if (!wait_begin(&bits_ptr)) {
			continue;
		}
		uint8_t bytes[20];
		for (int i = 0; i < 100; ++i) {
			printf("bit %d: ", i);
			int bit = read_protocol_bit(&bits_ptr);
			if (bit < 0) {
				float humidity = (float)(((uint16_t)bytes[0] << 8) + bytes[1]) / 10;
				float temp = (float)((((uint16_t)bytes[2] & 0x7F) << 8) + bytes[3]) / 10;
				printf("temp: %f°C, humidity: %f%%\n", temp, humidity);
				if (bytes[4] == (bytes[0] + bytes[1] + bytes[2] + bytes[3])) {
					fprintf(stderr, "valid checksum\n");
				} else {
					fprintf(stderr, "invalid checksum\n");
				}
				break;
			}
			bytes[i / 8] <<= 1;
			bytes[i / 8] |= bit;
		}
		sleep(2);
	}
	ftdi_disable_bitbang(ftdi);

	ftdi_usb_close(ftdi);
done:
	ftdi_free(ftdi);

	return retval;
}
