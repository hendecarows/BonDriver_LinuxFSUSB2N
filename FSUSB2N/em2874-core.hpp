// EM2874 core

#ifndef _EM2874CORE_HPP_
#define _EM2874CORE_HPP_

#include <inttypes.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include "IoThread.hpp"

#define EM28XX_REG_I2C_CLK		0x06
#define EM2874_REG_TS_ENABLE	0x5F
#define EM28XX_REG_GPIO		0x80

#define EM2874_EP_TS1		0x84

#define DEMOD_ADDR	0x20
#define EEPROM_ADDR	0xa0
#define TUNER_ADDR	0xc0

/* EM2874 TS Enable Register (0x5f) */
#define EM2874_TS1_CAPTURE_ENABLE 0x01
#define EM2874_TS1_FILTER_ENABLE  0x02
#define EM2874_TS1_NULL_DISCARD   0x04

class EM2874Device
{
public:
	EM2874Device () = default;
	virtual ~EM2874Device ();

	bool openDevice (const std::string &devfile);
	void closeDevice();

	uint8_t readReg (const uint8_t idx);
	int readReg (const uint8_t idx, uint8_t *val);
	int writeReg (const uint8_t idx, const uint8_t val);
	bool readI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop);
	bool writeI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop);

	bool isStreaming() const { return isStream; }
	int getDeviceID();
	void startStream();
	void stopStream();
	void resumeStream();
	void pauseStream();
	int getStream(const void **ptr);

private:
	int fd = -1;
	std::atomic<bool> isStream{false};
	std::unique_ptr<TsIoThread> ts_func;
	std::unique_ptr<std::thread> ts_thread;

	bool openUsbDevice (const char *devfile);
};

#endif

