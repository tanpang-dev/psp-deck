/* sceUsbBus_driver ABI with the class-specific "extra" descriptor fields (layout as documented by uOFW).
 * pspsdk's pspusbbus.h lacks those fields, which the UAC interfaces need. */
#ifndef DECK_USBBD_H
#define DECK_USBBD_H

#define PSP_USBBUS_DRIVERNAME "USBBusDriver"

struct DeviceDescriptor {
    unsigned char  bLength, bDescriptorType;
    unsigned short bcdUSB;
    unsigned char  bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    unsigned short idVendor, idProduct, bcdDevice;
    unsigned char  iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} __attribute__((aligned(4)));

struct EndpointDescriptor {
    unsigned char  bLength, bDescriptorType, bEndpointAddress, bmAttributes;
    unsigned short wMaxPacketSize;
    unsigned char  bInterval;
    const unsigned char *extra;
    int extraLength;
};

struct InterfaceDescriptor {
    unsigned char bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints;
    unsigned char bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
    struct EndpointDescriptor *endpoints;
    const unsigned char *extra;
    int extraLength;
};

struct InterfaceSettings {
    struct InterfaceDescriptor *descriptors;  /* one per alternate setting */
    unsigned int alternateSetting;
    unsigned int numDescriptors;
};

struct ConfigDescriptor {
    unsigned char  bLength, bDescriptorType;
    unsigned short wTotalLength;
    unsigned char  bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, bMaxPower;
    struct InterfaceSettings *settings;
    const unsigned char *extra;
    int extraLength;
};

struct UsbConfiguration {
    struct ConfigDescriptor *configDescriptors;
    struct InterfaceSettings *settings;
    struct InterfaceDescriptor *interfaceDescriptors;
    struct EndpointDescriptor *endpointDescriptors;
};

struct UsbInterface { int expectNumber, interfaceNumber, numInterfaces; };
struct UsbEndpoint  { int driverEndpointNumber, endpointNumber, transmittedBytes; };

struct DeviceRequest {
    unsigned char  bmRequestType, bRequest;
    unsigned short wValue, wIndex, wLength;
};

struct StringDescriptor {
    unsigned char bLength, bDescriptorType;
    short bString[31];
};

struct UsbDriver {
    const char *driverName;
    int numEndpoints;                 /* including EP0 */
    struct UsbEndpoint *endpoints;
    struct UsbInterface *interface;
    struct DeviceDescriptor *descriptor_hi;
    struct UsbConfiguration *configuration_hi;
    struct DeviceDescriptor *descriptor;
    struct UsbConfiguration *configuration;
    struct StringDescriptor *stringDescriptors;
    int  (*processRequest)(int recipient, int arg, struct DeviceRequest *req);
    int  (*changeSetting)(int interfaceNumber, int alternateSetting);
    int  (*attach)(int speed);        /* 1 = full, 2 = hi */
    void (*detach)(void);
    void (*configure)(int speed, int count, struct InterfaceSettings *settings);
    int  (*start)(int size, void *args);
    int  (*stop)(int size, void *args);
    struct UsbDriver *link;
};

struct UsbbdDeviceRequest {
    struct UsbEndpoint *endpoint;
    void *data;
    int  size;
    int  isControlRequest;
    void (*onComplete)(struct UsbbdDeviceRequest *req);
    int  transmitted;
    int  returnCode;                  /* 0 = ok, -3 = cancelled */
    struct UsbbdDeviceRequest *next;
    void *arg;
    void *physicalAddress;
};

#ifndef DECK_HOST_TEST  /* host builds (desc_check) have 64-bit pointers */
_Static_assert(sizeof(struct DeviceDescriptor) == 20, "abi");
_Static_assert(sizeof(struct EndpointDescriptor) == 16, "abi");
_Static_assert(sizeof(struct InterfaceDescriptor) == 24, "abi");
_Static_assert(sizeof(struct ConfigDescriptor) == 24, "abi");
_Static_assert(sizeof(struct StringDescriptor) == 64, "abi");
#endif

int sceUsbbdRegister(struct UsbDriver *drv);
int sceUsbbdUnregister(struct UsbDriver *drv);
int sceUsbbdReqSend(struct UsbbdDeviceRequest *req);
int sceUsbbdReqRecv(struct UsbbdDeviceRequest *req);
int sceUsbbdReqCancel(struct UsbbdDeviceRequest *req);
int sceUsbbdReqCancelAll(struct UsbEndpoint *endp);
int sceUsbbdClearFIFO(struct UsbEndpoint *endp);
int sceUsbbdStall(struct UsbEndpoint *endp);

int sceUsbStart(const char *name, int size, void *args);
int sceUsbStop(const char *name, int size, void *args);
int sceUsbActivate(unsigned int pid);
int sceUsbDeactivate(unsigned int pid);

#endif
