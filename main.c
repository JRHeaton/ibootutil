#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/USB.h>
#include <IOKit/usb/USBSpec.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>

#define RECOVERY 0x1281
#define DFU 0x1222
#define WTF 0x1227

struct iBootUSBConnection {
	io_service_t usbService;
	IOUSBDeviceInterface **deviceInterface;
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
	IOUSBDeviceInterface **deviceInterface;
	
	SInt32 score;
	if(IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pluginInterface, &score) != 0) {
		printf("Error: couldn't create plugin interface for device\n");
		IOObjectRelease(service);
		return NULL;
	}
	
	if((*pluginInterface)->QueryInterface(pluginInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
										  (LPVOID*)&deviceInterface) != 0) {
		printf("Error: couldn't create device interface\n");
		IOObjectRelease(service);
		return NULL;
	}
	
	(*pluginInterface)->Release(pluginInterface);
	
	if((*deviceInterface)->USBDeviceOpen(deviceInterface) != 0) { 
		printf("Error: couldn't connect to device\n");
		IOObjectRelease(service);
		(*deviceInterface)->Release(deviceInterface);
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
	connection->deviceInterface = deviceInterface;	
	connection->name = productName;
	connection->serial = productSerial;
	connection->idProduct = productID;
	
	return connection;
}

void iDevice_close(iBootUSBConnection connection) {
	if(connection != NULL) {
		printf("Closing connection...\n");
		if(connection->usbService) IOObjectRelease(connection->usbService);
		if(connection->deviceInterface) (*connection->deviceInterface)->Release(connection->deviceInterface);
		if(connection->name) CFRelease(connection->name);
		if(connection->serial) CFRelease(connection->serial);
		
		free(connection);
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
	if(connection == NULL || command == NULL || connection->idProduct == WTF)
		return -1;
	
	IOUSBDevRequest request;
	request.bmRequestType = 0x40;
	request.bRequest = 0x0;
	request.wValue = 0x0;
	request.wIndex = 0x0;
	request.wLength = (UInt16)(strlen(command)+1);
	request.pData = (void *)command;
	request.wLenDone = 0x0;
	
	if((*connection->deviceInterface)->DeviceRequest(connection->deviceInterface, &request) != kIOReturnSuccess) {
		return -1;
	}
	
	return 0;
}

int iDevice_send_file(iBootUSBConnection connection, unsigned char *data) {
	if(connection == NULL || data == NULL)
		return -1;
	
	
}

void iDevice_reset(iBootUSBConnection connection) {
	if(connection == NULL) 
		return;
	
	(*connection->deviceInterface)->ResetDevice(connection->deviceInterface);
	iDevice_close(connection);
	exit(0);
}

int main (int argc, const char * argv[]) {
	iBootUSBConnection connection = iDevice_open(RECOVERY);
	if(connection == NULL) {
		printf("Error: couldn't find device\n");
	}
	
	iDevice_print(connection);
	iDevice_send_command(connection, "bgcolor 255 12 255");
	iDevice_close(connection);
	
	return 0;
}
