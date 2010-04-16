#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/USB.h>
#include <IOKit/usb/USBSpec.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <sys/stat.h>
#include <readline/readline.h>

#define RECOVERY 0x1281
#define DFU 0x1227

#define REQUEST_COMMAND 0x40
#define REQUEST_FILE 0x21
#define REQUEST_STATUS 0xA1
#define RESPONSE_PIPE 0x81

struct iBootUSBConnection {
	io_service_t usbService;
	IOUSBDeviceInterface **deviceHandle;
	CFStringRef name;
	CFStringRef serial;
	unsigned int idProduct;
};
typedef struct iBootUSBConnection *iBootUSBConnection;

iBootUSBConnection iDevice_open(uint32_t productID) {
	CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
	if(match == NULL) {
		printf("Error: couldn't create query dictionary for IOServiceGetMatchingService()\n");
		return NULL;
	}
	
	uint32_t vendorID = kAppleVendorID;
	CFNumberRef idVendor = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendorID);
	CFNumberRef idProduct = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &productID);
	
	CFDictionarySetValue(match, CFSTR(kUSBVendorID), idVendor);
	CFDictionarySetValue(match, CFSTR(kUSBProductID), idProduct);
	
	CFRelease(idVendor);
	CFRelease(idProduct);
	
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault, match);
	if(!service) {
		return NULL;
	}
	
	IOCFPlugInInterface **pluginInterface;
	IOUSBDeviceInterface **deviceHandle;
	
	SInt32 score;
	if(IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pluginInterface, &score) != 0) {
		printf("Error: couldn't create plugin interface for device\n");
		IOObjectRelease(service);
		return NULL;
	}
	
	if((*pluginInterface)->QueryInterface(pluginInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
										  (LPVOID*)&deviceHandle) != 0) {
		printf("Error: couldn't create device interface\n");
		IOObjectRelease(service);
		return NULL;
	}
	
	(*pluginInterface)->Release(pluginInterface);
	
	if((*deviceHandle)->USBDeviceOpen(deviceHandle) != 0) { 
		printf("Error: couldn't connect to device\n");
		IOObjectRelease(service);
		(*deviceHandle)->Release(deviceHandle);
		return NULL;
	}
	
	CFMutableDictionaryRef properties;
	IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault, 0);
	CFStringRef productName = CFDictionaryGetValue(properties, CFSTR(kUSBProductString));
	CFStringRef productSerial = CFDictionaryGetValue(properties, CFSTR(kUSBSerialNumberString));
	CFRelease(properties);
	
	iBootUSBConnection connection = malloc(sizeof(struct iBootUSBConnection));
	memset(connection, '\0', sizeof(struct iBootUSBConnection));

	connection->usbService = service;
	connection->deviceHandle = deviceHandle;	
	connection->name = productName;
	connection->serial = productSerial;
	connection->idProduct = productID;
	
	return connection;
}

void iDevice_close(iBootUSBConnection connection) {
	if(connection != NULL) {
		printf("Closing connection...\n");
//		if(connection->usbService) IOObjectRelease(connection->usbService);
//		if(connection->deviceHandle) (*connection->deviceHandle)->USBDeviceClose(connection->deviceHandle);
//		if(connection->deviceHandle) (*connection->deviceHandle)->Release(connection->deviceHandle);
//		if(connection->name) CFRelease(connection->name);
//		if(connection->serial) CFRelease(connection->serial);
//		
//		free(connection);
	}
}

void iDevice_print(iBootUSBConnection connection) {
	if(connection != NULL) {
		if(connection->name && connection->serial) {
			CFShow(connection->name);
			CFShow(connection->serial);
		}
	}
}

int iDevice_send_command(iBootUSBConnection connection, const char *command) {
	if(connection == NULL || command == NULL || connection->idProduct != RECOVERY)
		return -1;
	
	IOUSBDevRequest request;
	request.bmRequestType = REQUEST_COMMAND;
	request.bRequest = 0x0;
	request.wValue = 0x0;
	request.wIndex = 0x0;
	request.wLength = (UInt16)(strlen(command)+1);
	request.pData = (void *)command;
	request.wLenDone = 0x0;
	
	if((*connection->deviceHandle)->DeviceRequest(connection->deviceHandle, &request) != kIOReturnSuccess) {
		if(strcmp(command, "reboot") != 0) 
			printf("Error sending command\n");
		else {
			printf("Rebooting device...\n");
			iDevice_close(connection);
			exit(0);
		}

		return -1;
	} 
	
	printf("Sent command: %s\n", command);
	
	return 0;
}

int iDevice_request_status(iBootUSBConnection connection, int flag) {
	if(connection == NULL)
		return -1;
	
	IOUSBDevRequest status_request;
	char response[6];
	
	status_request.bmRequestType = REQUEST_STATUS;
	status_request.bRequest = 0x3;
	status_request.wValue = 0x0;
	status_request.wIndex = 0x0;
	status_request.wLength = 0x6;
	status_request.pData = (void *)response;
	status_request.wLenDone = 0x0;
	
	if((*connection->deviceHandle)->DeviceRequest(connection->deviceHandle, &status_request) != kIOReturnSuccess) {
		printf("Error: couldn't receive status\n");
		return -1;
	}
	
	if(response[4] != flag) {
		printf("Error: invalid status response\n");
		return -1;
	}
	
	return 0;
}

int iDevice_send_file(iBootUSBConnection connection, const char *path) {
	if(connection == NULL || path == NULL)
		return -1;
	
	unsigned char *buf;
	unsigned int packet_size = 0x800;
	struct stat check;
	
	if(stat(path, &check) != 0) {
		printf("File doesn't exist: %s\n", path);
		return -1;
	}
	
	buf = malloc(check.st_size);
	memset(buf, '\0', check.st_size);
	
	FILE *file = fopen(path, "r");
	if(file == NULL) {
		printf("Couldn't open file: %s\n", path);
		return -1;
	}

	if(fread((void *)buf, check.st_size, 1, file) == 0) {
		printf("Couldn't create buffer\n");
		fclose(file);
		free(buf);
		return -1;
	}
	
	fclose(file);
	
	unsigned int packets, current;
	packets = (check.st_size / packet_size);
	if(check.st_size % packet_size) {
		packets++;
	}
	
	for(current = 0; current < packets; ++current) {
		int size = (current + 1 < packets ? packet_size : (check.st_size % packet_size));
		
		IOUSBDevRequest file_request;
		
		file_request.bmRequestType = REQUEST_FILE;
		file_request.bRequest = 0x1;
		file_request.wValue = current;
		file_request.wIndex = 0x0;
		file_request.wLength = (UInt16)size;
		file_request.pData = (void *)&buf[current * packet_size];
		file_request.wLenDone = 0x0;
		
		if((*connection->deviceHandle)->DeviceRequest(connection->deviceHandle, &file_request) != kIOReturnSuccess) {
			printf("Error: couldn't send packet %d\n", current + 1);
			free(buf);
			return -1;
		}
		
		if(iDevice_request_status(connection, 5) != 0) {
			free(buf);
			return -1;
		}
	}
	
	IOUSBDevRequest checkup;
	checkup.bmRequestType = REQUEST_FILE;
	checkup.bRequest = 0x1;
	checkup.wValue = current;
	checkup.wIndex = 0x0;
	checkup.wLength = 0x0;
	checkup.pData = buf;
	checkup.wLenDone = 0x0;
	
	(*connection->deviceHandle)->DeviceRequest(connection->deviceHandle, &checkup);
	
	for(current = 6; current < 8; ++current) {
		if(iDevice_request_status(connection, current) != 0) {
			free(buf);
			return -1;
		}
	}
	
	free(buf);
	printf("Sent file %s\n", path);
	
	return 0;
}

void iDevice_reset(iBootUSBConnection connection) {
	if(connection == NULL) 
		return;
	
	(*connection->deviceHandle)->ResetDevice(connection->deviceHandle);
	iDevice_close(connection);
	exit(0);
}

int iDevice_start_shell(iBootUSBConnection connection, const char *prompt) {
	if(connection == NULL)
		return -1;

	const char *input;
	do {
		input = readline(prompt);
		if(input[0] == '/') {
			if(strcmp(input, "/exit") == 0) {
				iDevice_close(connection);
				exit(0);
			} else if (strcmp(input, "/reset") == 0) {
				iDevice_reset(connection);
				exit(0);
			} else if(strstr(input, "/sendfile") != NULL) {
				const char *file = (const char *)&input[strlen("/sendfile")+1];
				iDevice_send_file(connection, file);
			}
		} else {
			iDevice_send_command(connection, input);
		}
	} while(1);
	
	return 0;
}

int main (int argc, const char **argv) {
	iBootUSBConnection connection = iDevice_open(RECOVERY);
	if(connection == NULL) {
		printf("Error: couldn't find device\n");
	}
	
	iDevice_print(connection);
	iDevice_start_shell(connection, "iDevice$ ");
	iDevice_close(connection);
	
	return 0;
}
