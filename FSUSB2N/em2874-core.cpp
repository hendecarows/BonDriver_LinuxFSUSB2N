
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <plog/Log.h>

#include "usbops.hpp"
#include "em2874-core.hpp"

#define USBREQ_TIMEOUT	100
#define I2C_TIMEOUT		200

#define EM28XX_REG_I2C_RET		0x05
#define EM28XX_REG_CHIPID		0x0A

#define EM2874_REG_CAS_STATUS	0x70
#define EM2874_REG_CAS_DATALEN	0x71
#define EM2874_REG_CAS_MODE1	0x72
#define EM2874_REG_CAS_RESET	0x73
#define EM2874_REG_CAS_MODE2	0x75

#define TARGET_ID_VENDOR	0x0511
#define TARGET_ID_PRODUCT	0x0029
#define TARGET_ID_PRODUCT2	0x003b

const char BASE_DIR_UDEV[]	= "/dev/bus/usb"; // udev_USB
const char BASE_DIR_USBFS[]	= "/proc/bus/usb"; // usbfs

inline uint8_t ICC_checkSum (const uint8_t* data, int len)
{
	uint8_t sum = 0;
	for ( ; len > 0; len-- ) {
		sum ^= *data++;
	}
	return sum;
}

EM2874Device::~EM2874Device ()
{
	closeDevice();
}

bool EM2874Device::openUsbDevice (const char *devfile)
{
	usb_device_descriptor usb_desc;
	usb_getdesc(devfile, &usb_desc);
	if(usb_desc.idVendor != TARGET_ID_VENDOR ||
	(usb_desc.idProduct != TARGET_ID_PRODUCT && usb_desc.idProduct != TARGET_ID_PRODUCT2))
		return false;
	fd = usb_open(devfile);
	if(fd == -1)
		return false;
	usb_claim(fd, 0);

	uint8_t val;

	if(readReg(0x0A, &val) && readReg(0x0C, &val) && readReg(0x0B, &val)){
		writeReg(EM28XX_REG_GPIO, 0xFF);
	}else{
		return false;
	}
	if(writeReg(0x0C, 0x10)
	&& writeReg(0x12, 0x27)
	&& writeReg(EM28XX_REG_GPIO, 0xFE)
	&& writeReg(EM2874_REG_CAS_MODE1, 0x0))
	{
		writeReg(0x13, 0x10);
		writeReg(0x10, 0);
		writeReg(0x11, 0x11);
		writeReg(0x28, 0x01);
		writeReg(0x29, 0xff);
		writeReg(0x2a, 0x01);
		writeReg(0x2b, 0xff);
		writeReg(0x1c, 0);
		writeReg(0x1d, 0);
		writeReg(0x1e, 0);
		writeReg(0x1f, 0);
		writeReg(0x1b, 0);
		writeReg(0x5e, 128);
		writeReg( EM2874_REG_TS_ENABLE, 0 );
		writeReg(EM2874_REG_CAS_MODE1, 0x0);
		return true;
	}

	return false;
}

bool EM2874Device::openDevice(const std::string &devfile)
{
	// devfile が指定されている場合：単一ファイルのオープンを試みる
	if (!devfile.empty()) {
		return openUsbDevice(devfile.c_str());
	}

	// devfile が指定されていない場合：USBデバイスの自動検索を行う
	std::filesystem::path base_dir = std::filesystem::path(BASE_DIR_UDEV);
	if (!std::filesystem::exists(base_dir) || !std::filesystem::is_directory(base_dir)) {
		base_dir = std::filesystem::path(BASE_DIR_USBFS);
	}

	if (!std::filesystem::exists(base_dir)) {
		return false;
	}

	for (const auto& bus_entry : std::filesystem::directory_iterator(base_dir)) {
		if (bus_entry.is_directory()) {
			for (const auto& dev_entry : std::filesystem::directory_iterator(bus_entry.path())) {
				if (!dev_entry.is_directory()) {
					if (openUsbDevice(dev_entry.path().string().c_str())) {
						return true; // 最初に見つかった対応デバイスを開いて成功を返す
					}
				}
			}
		}
	}

	return false;
}

void EM2874Device::closeDevice()
{
	if (fd == -1)
		return;

	stopStream();
	writeReg(EM2874_REG_TS_ENABLE, 0);
	writeReg(EM28XX_REG_GPIO, 0xFF);
	writeReg(EM2874_REG_CAS_MODE1, 0x0);
	writeReg(0x0C, 0x0);
	usb_release(fd, 0);
	close(fd);
	fd = -1;
}

uint8_t EM2874Device::readReg (const uint8_t idx)
{
	uint8_t val;
	if (!readReg (idx, &val)) {
		PLOGE << "failed to read register " << static_cast<int>(idx);
	}
	return val;
}

int EM2874Device::readReg (const uint8_t idx, uint8_t *val)
{
	usbdevfs_ctrltransfer ctrl1 = {USB_DIR_IN |USB_TYPE_VENDOR|USB_RECIP_DEVICE, 0, 0, idx, 1, USBREQ_TIMEOUT, val};
	return usb_ctrl(fd, &ctrl1);
}

int EM2874Device::writeReg (const uint8_t idx, const uint8_t val)
{
	uint8_t sbuf[1] = {val};
	usbdevfs_ctrltransfer ctrl1 = {USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE, 0, 0, idx, 1, USBREQ_TIMEOUT, sbuf};
	return usb_ctrl(fd, &ctrl1);
}

bool EM2874Device::readI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop)
{
	uint8_t is_stop = isStop ? 2 : 3;
	usbdevfs_ctrltransfer ctrl1 = {
			USB_DIR_IN |USB_TYPE_VENDOR|USB_RECIP_DEVICE,
			is_stop, 0, addr, size, USBREQ_TIMEOUT, data
	};
	if(usb_ctrl(fd, &ctrl1) < 0)
		return false;

	uint8_t ret = readReg(EM28XX_REG_I2C_RET);
	return ret == 0 ? true : false;
}

bool EM2874Device::writeI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop)
{
	uint8_t is_stop = isStop ? 2 : 3;
	usbdevfs_ctrltransfer ctrl1 = {
			USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE,
			is_stop, 0, addr, size, USBREQ_TIMEOUT, data
	};
	if(usb_ctrl(fd, &ctrl1) < 0)
		return false;

	uint8_t ret = readReg(EM28XX_REG_I2C_RET);
	return ret == 0 ? true : false;
}

int EM2874Device::getDeviceID()
{
	uint8_t buf[2], tuner_reg;
	// ROMで判断
	writeReg(EM28XX_REG_I2C_CLK, 0x42);
	buf[0] = 0; buf[1] = 0x6a;	writeI2C(EEPROM_ADDR, 2, buf, false);
	if(!readI2C (EEPROM_ADDR, 2, buf, true))
		return -1;

	if(buf[0] == 0x3b && buf[1] == 0x00)
		return 2;

	// Tuner Regで判断
	writeReg(EM28XX_REG_I2C_CLK, 0x44);
	buf[0] = 0xfe; buf[1] = 0;	writeI2C(DEMOD_ADDR, 2, buf, true);
	tuner_reg = 0x0;
	writeI2C(TUNER_ADDR, 1, &tuner_reg, false);
	readI2C (TUNER_ADDR, 1, &tuner_reg, true);

	buf[0] = 0xfe; buf[1] = 1;	writeI2C(DEMOD_ADDR, 2, buf, true);

	if(tuner_reg == 0x84)	// TDA18271HD
		return 1;

	return 2;
}

/* */

void EM2874Device::startStream()
{
	if (isStream) return;

	writeReg(EM2874_REG_TS_ENABLE, EM2874_TS1_CAPTURE_ENABLE | EM2874_TS1_NULL_DISCARD);
	usb_setinterface(fd, 0, 1);
	ts_func = std::make_unique<TsIoThread>(fd, EM2874_EP_TS1);
	ts_thread = std::make_unique<std::thread>(std::ref(*ts_func));
	isStream = true;
}

void EM2874Device::stopStream()
{
	if (!isStream) return;

	writeReg(EM2874_REG_TS_ENABLE, 0);
	ts_func->cancel();
	if (ts_thread && ts_thread->joinable()) {
		ts_thread->join();
	}
	ts_thread.reset();
	ts_func.reset();

	isStream = false;
}
void EM2874Device::resumeStream()
{
	if (!isStream) return;

	writeReg(EM2874_REG_TS_ENABLE, EM2874_TS1_CAPTURE_ENABLE | EM2874_TS1_NULL_DISCARD);
}

void EM2874Device::pauseStream()
{
	if (!isStream) return;

	writeReg(EM2874_REG_TS_ENABLE, 0);
}

int EM2874Device::getStream(const void **ptr)
{
	if (!isStream) return 0;
	return ts_func->readBuffer(ptr);
}

/* */

