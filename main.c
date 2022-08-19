#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define DANGEROUSLY_ALLOW_CHANGING_CO2_REFERENCE false

float convertCtoF(float celcius) {
	return celcius*1.8+32;
}

int delay(long ms) {
	if (ms < 0) {
		errno = EINVAL;
		return -1;
	}

	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;

	int result;
	do {
		result = nanosleep(&ts, &ts);
	} while (result && errno == EINTR);

	return result;
}

uint8_t crc8(const uint8_t *data, int len) {
	const uint8_t POLYNOMIAL = 0x31;
	uint8_t crc = 0xFF;

	for (int j = len; j; --j) {
		crc ^= *data++;

		for (int i = 8; i; --i) {
			crc = (crc & 0x80) ? (crc << 1) ^ POLYNOMIAL : (crc << 1);
		}
	}
	return crc;
}

int readRegister(int fileDescriptor, uint16_t registerAddress) {
	uint8_t registerBuffer[2];
	registerBuffer[0] = (registerAddress >> 8) & 0xFF;
	registerBuffer[1] = registerAddress & 0xFF;
	if (write(fileDescriptor, registerBuffer, sizeof(registerBuffer)) < 0) return -1;

	/////

	delay(4);

	/////

	unsigned char buffer[2] = {0};
	if (read(fileDescriptor, buffer, sizeof(buffer)) < 0) return -1;
	return (uint16_t)(buffer[0] << 8 | (buffer[1] & 0xFF));
}

ssize_t sendCommand(int fileDescriptor, uint16_t command) {
	uint8_t buffer[2];
	buffer[0] = (command >> 8) & 0xFF;
	buffer[1] = command & 0xFF;
	return write(fileDescriptor, buffer, sizeof(buffer));
}

ssize_t sendCommandWithArgument(int fileDescriptor, uint16_t command, uint16_t argument) {
	uint8_t buffer[5];
	buffer[0] = (command >> 8) & 0xFF;
	buffer[1] = command & 0xFF;
	buffer[2] = argument >> 8;
	buffer[3] = argument & 0xFF;
	buffer[4] = crc8(buffer + 2, 2);
	return write(fileDescriptor, buffer, sizeof(buffer));
}


/////


struct Measurements {
	float CO2;
	float relativeHumidity;
	float temperature;
};
bool readMeasurements(int i2cFileDescriptor, struct Measurements **measurements) {
	uint8_t buffer[18];
	const uint16_t measurementAddress = 0x0300;
	buffer[0] = (measurementAddress >> 8) & 0xFF;
	buffer[1] = measurementAddress & 0xFF;

	if (write(i2cFileDescriptor, buffer, 2) == -1) return false;

	delay(4);

	if (read(i2cFileDescriptor, buffer, 18) == -1) return false;

	// loop through the bytes we read, 3 at a time for i=MSB, i+1=LSB, i+2=CRC
	for (uint8_t i = 0; i < 18; i += 3) {
		if (crc8(buffer + i, 2) != buffer[i + 2]) {
			// we got a bad CRC, fail out
			return false;
		}
	}

	// CRCs are good, unpack floats

	struct Measurements *m = malloc(sizeof(struct Measurements));

	uint32_t CO2 = 0;
	CO2 |= buffer[0];
	CO2 <<= 8;
	CO2 |= buffer[1];
	CO2 <<= 8;
	CO2 |= buffer[3];
	CO2 <<= 8;
	CO2 |= buffer[4];
	memcpy(&m->CO2, &CO2, sizeof(m -> CO2));

	uint32_t temperature = 0;
	temperature |= buffer[6];
	temperature <<= 8;
	temperature |= buffer[7];
	temperature <<= 8;
	temperature |= buffer[9];
	temperature <<= 8;
	temperature |= buffer[10];
	memcpy(&m->temperature, &temperature, sizeof(m -> temperature));

	uint32_t relativeHumidity = 0;
	relativeHumidity |= buffer[12];
	relativeHumidity <<= 8;
	relativeHumidity |= buffer[13];
	relativeHumidity <<= 8;
	relativeHumidity |= buffer[15];
	relativeHumidity <<= 8;
	relativeHumidity |= buffer[16];
	memcpy(&m->relativeHumidity, &relativeHumidity, sizeof(m -> relativeHumidity));

	*measurements = m;

	return true;
}

bool isMeasurementDataAvailable(int i2cFileDescriptor) {
	const uint16_t dataReadyAddress = 0x0202;
	int result = readRegister(i2cFileDescriptor, dataReadyAddress);
	if (result < 0) return false;
	return result == 1;
}

bool performSoftReset(int i2cFileDescriptor) {
	const uint16_t softResetAddress = 0xD304;
	int sizeWritten = sendCommand(i2cFileDescriptor, softResetAddress);
	delay(30);
	return sizeWritten > 0;
}

const uint16_t forcedRecalibrationReferenceAddress = 0x5204;
uint16_t getCalibrationReference(int i2cFileDescriptor) {
	return readRegister(i2cFileDescriptor, forcedRecalibrationReferenceAddress);
}
bool setCalibrationReference(int i2cFileDescriptor, uint16_t referenceAmount) {
	if (!DANGEROUSLY_ALLOW_CHANGING_CO2_REFERENCE) return false;
	if ((referenceAmount < 400) || (referenceAmount > 2000)) return false;
	return sendCommandWithArgument(i2cFileDescriptor, forcedRecalibrationReferenceAddress, referenceAmount) > 0;
}

const uint16_t automaticSelfCalibrationAddress = 0x5306;
bool isAutomaticSelfCalibrationEnabled(int i2cFileDescriptor) {
	int result = readRegister(i2cFileDescriptor, automaticSelfCalibrationAddress);
	return result == 1;
}
bool setAutomaticSelfCalibration(int i2cFileDescriptor, bool enabled) {
	return sendCommandWithArgument(i2cFileDescriptor, automaticSelfCalibrationAddress, enabled) > 0;
}

const uint16_t measurementIntervalAddress = 0x4600;
uint16_t getMeasurementInterval(int i2cFileDescriptor) {
	return readRegister(i2cFileDescriptor, measurementIntervalAddress);
}
bool setMeasurementInterval(int i2cFileDescriptor, uint16_t intervalAmount) {
	if ((intervalAmount < 2) || (intervalAmount > 1800)) return false;
	return sendCommandWithArgument(i2cFileDescriptor, measurementIntervalAddress, intervalAmount) > 0;
}

int initSCD30() {
	int i2cFileDescriptor = open("/dev/i2c-1", O_RDWR);

	if (i2cFileDescriptor < 0) return -1;

	const uint16_t scd30Address = 0x61;
	if (ioctl(i2cFileDescriptor, I2C_SLAVE, scd30Address) < 0) return -1;

	return i2cFileDescriptor;
}

void printError(int errorNumber) {
	if (errorNumber < 0) {
		printf("errno=%d, err_msg=\"%s\"\n", errorNumber, strerror(errorNumber));
	}
}

/////

// ./scd30 -r 600

int main(int argc, char *argv[]) {
	const int fd = initSCD30();
	if (fd < 0) {
		printf("Can't interact with SCD30. Error: %s\n", strerror(errno));
		return 1;
	}

	if (argc == 3) {
		if (strcmp(argv[1], "-r") == 0) {
			int reference = atoi(argv[2]);
			// To get this to work, set the DANGEROUSLY_ALLOW_CHANGING_CO2_REFERENCE flag above to true. Remember to set it back to false when you're done.
			if (reference > 0 && setCalibrationReference(fd, reference)) {
				printf("Force recalibration completed!\n");
				uint16_t reference = getCalibrationReference(fd);
				if (reference > 0) {
					printf("CO2 calibration reference: %dppm\n", reference);
				} else {
					printf("CO2 calibration reference: N/A\n");
				}
			} else {
				printf("Force recalibration failed\n");
			}
		}

		return EXIT_SUCCESS;
	}

	if (argc == 2) {
		if (strcmp(argv[1], "-h") == 0) {
			printf("-h	help text\n");
			printf("-r	calibrate CO2 sensor (include desired ppm at current aproximate levels)\n");
			printf("-c	CO2 level\n");
			printf("-m	humidity level\n");
			printf("-t	temperature in celsius\n");
			printf("-T	temperature in fahrenheit\n");
			printf("-F	output to files INSTEAD of stdout\n");
			printf("-u	truncate output\n");
			printf("-o	continuous output (default should be a single run of the loop)\n");
		}
		return EXIT_SUCCESS;
	}

	while (true) {
		FILE *fp_1 = fopen("CO2.txt", "w");
		if(fp_1 == NULL) {                         
			printf("File can't be opened\n");
			exit(1);
		}

		FILE *fp_2 = fopen("humidity.txt", "w");
		if(fp_2 == NULL) {
			printf("File can't be opened\n");
			exit(1);
		}

		FILE *fp_3 = fopen("temp.txt", "w");
		if(fp_3 == NULL) {
			printf("File can't be opened\n");
			exit(1);
		}

		if (isMeasurementDataAvailable(fd)) {
			struct Measurements *measurements = NULL;
			if (readMeasurements(fd, &measurements)) {

				printf(
					"CO2: %.2fppm   Temp: %.2fF   Humidity: %.2frH\n",
					measurements->CO2,
					convertCtoF(measurements->temperature) - 5,
					measurements->relativeHumidity
				);

				printError(fprintf(
					fp_1,
					// "CO2: %.2f ppm",
					"%.2f",
					measurements->CO2
				));
				printError(fprintf(
					fp_2,
					// "Humidity: %.2frH",
					"%.2f",
					measurements->relativeHumidity
				));
				printError(fprintf(
					fp_3,
					// "Temp: %.2fF",
					"%.2f",
					convertCtoF(measurements->temperature) - 5
				));
				
			}
		}

		fclose(fp_1);
		fclose(fp_2);
		fclose(fp_3);

		delay(5000);
	}
}
