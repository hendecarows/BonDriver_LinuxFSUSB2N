// USB操作

#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <sys/file.h>

#include <plog/Log.h>

#include "usbops.hpp"

void
usb_getdesc(const char *devfile, usb_device_descriptor* desc)
{
	int f = open(devfile, O_RDONLY);
	if (-1 == f) {
		PLOGE << "can't open usbdevfile to read '" << devfile << "'";
		return;
	}
	
	memset(desc, 0, sizeof(usb_device_descriptor));
	ssize_t rlen = read(f, desc, sizeof(usb_device_descriptor));
	if (-1 == rlen) {
		PLOGE << "can't read usbdevfile '" << devfile << "'";
	}
	close(f);
}

int
usb_open(const char *devfile)
{
	// open
	int fd = open(devfile, O_RDWR);
	if (-1 == fd) {
		int errno_bak = errno;
		PLOGE << "usb open failed" << errno_bak << "!!";
	}else if(::flock(fd, LOCK_EX | LOCK_NB) < 0) {
		close(fd);
		fd = -1;
		PLOGE << "share violation";
	}
	return fd;
}
void
usb_claim(int fd, unsigned int interface)
{
	int r = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface);
	if (r < 0) {
		int errno_bak = errno;
		if (errno_bak == EBUSY) { // BUSY?
			PLOGE << "usb interface busy.";
			return;
		}
		
		// failed
		PLOGE << "usb claim failed: " << errno_bak;
	}
}

void
usb_release(int fd, unsigned int interface)
{
	int r = ioctl(fd, USBDEVFS_RELEASEINTERFACE, &interface);
	if (r < 0) {
		int errno_bak = errno;
		// failed
		PLOGE << "usb release failed: " << errno_bak;
	}
}

int
usb_setinterface(int fd, const unsigned int interface, const unsigned int altsetting)
{
	usbdevfs_setinterface setintf;
	setintf.interface = interface;
	setintf.altsetting = altsetting;
	return ioctl(fd, USBDEVFS_SETINTERFACE, &setintf);
}

int
usb_ctrl(int fd, usbdevfs_ctrltransfer *ctrl)
{
	int r = ioctl(fd, USBDEVFS_CONTROL, ctrl);
	if (r < 0) {
		int errno_bak = errno;
		// failed
		PLOGE << "usb ctrl failed: " << errno_bak;
	}
	return r;
}

int
usb_submiturb(int fd, usbdevfs_urb* urbp)
{
	return ioctl(fd, USBDEVFS_SUBMITURB, urbp);
}

int
usb_reapurb_ndelay(int fd, usbdevfs_urb** urbpp)
{
	return ioctl(fd, USBDEVFS_REAPURBNDELAY, (void *)urbpp);
}

int
usb_discardurb(int fd, usbdevfs_urb* urbp)
{
	return ioctl(fd, USBDEVFS_DISCARDURB, urbp);
}

