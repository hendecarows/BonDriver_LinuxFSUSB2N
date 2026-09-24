// TS I/O発行・受信スレッド

#ifndef _IO_THREAD_HPP_
#define _IO_THREAD_HPP_

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <vector>
#include "usbops.hpp"


#define IOREQ_RESERVE_NUM 20

class TsIoThread
{
public:
	TsIoThread (const int fd, const int endpoint);
	virtual ~TsIoThread() = default;
	void operator()();
	void cancel();
	int readBuffer(const void **ptr);

protected:
	int fd;
	int endpoint;
	std::atomic<bool> is_cancelled{false};
	usbdevfs_urb urb[IOREQ_RESERVE_NUM];
	std::vector<uint8_t> buf;
	std::vector<size_t> actualSize;
	std::atomic<int> buf_pushIndex = 0;
	std::atomic<int> buf_popIndex = 0;
	std::atomic<int> issuedRequestNum = 0;

	void SetBufferPtrOfUrb(usbdevfs_urb* urbp);
	void reapUrbs();
};

#endif

