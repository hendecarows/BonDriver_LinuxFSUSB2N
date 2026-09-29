// BonDriver_LinuxFSUSB2N.hpp

#pragma once

#include "IBonDriver2.h"
#include "config.hpp"
#include "char_conv.hpp"
#include "stream_buffer.hpp"

#include "em2874-core.hpp"
#include "ktv.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>


namespace BonDriver_LinuxFSUSB2N {

class BonDriver final : public IBonDriver2
{
public:
	explicit BonDriver(config::Config &config);
	~BonDriver();

	// cannot copy
	BonDriver(const BonDriver &) = delete;
	BonDriver &operator=(const BonDriver &) = delete;

	// IBonDriver
	bool OpenTuner(void) override;
	void CloseTuner(void) override;

	bool SetChannel(const uint8_t bCh) override;
	float GetSignalLevel(void) override;

	uint32_t WaitTsStream(const uint32_t dwTimeOut = 0) override;
	uint32_t GetReadyCount(void) override;

	bool GetTsStream(uint8_t *pDst, uint32_t *pdwSize, uint32_t *pdwRemain) override;
	bool GetTsStream(uint8_t **ppDst, uint32_t *pdwSize, uint32_t *pdwRemain) override;

	void PurgeTsStream(void) override;

	void Release(void) override;

	// IBonDriver2
	const char16_t *GetTunerName(void) override;

	bool IsTunerOpening(void) override;

	const char16_t *EnumTuningSpace(const uint32_t dwSpace) override;
	const char16_t *EnumChannelName(const uint32_t dwSpace, const uint32_t dwChannel) override;

	bool SetChannel(const uint32_t dwSpace, const uint32_t dwChannel) override;

	uint32_t GetCurSpace(void) override;
	uint32_t GetCurChannel(void) override;

	static BonDriver *GetInstance();

private:
	static constexpr uint32_t TS_PACKET_SIZE = 188;
	static constexpr uint8_t TS_SYNC_BYTE = 0x47;
	static constexpr uint32_t INFINITE = 0xffffffff;
	static constexpr uint32_t WAIT_OBJECT_0 = 0x00000000;
	static constexpr uint32_t WAIT_ABANDONED = 0x00000080;
	static constexpr uint32_t WAIT_TIMEOUT = 0x00000102;
	static constexpr uint32_t STREAM_TIMEOUT = 0;

	enum class System { ISDB_T, ISDB_S, UNKNOWN };

	class Space final
	{
	public:
		class Channel final
		{
		public:
			Channel(const std::string &name_u8, const std::u16string &name, int32_t number, int32_t slot);
			~Channel() = default;

			// cannot copy
			Channel(const Channel &) = delete;
			Channel &operator=(const Channel &) = delete;

			// can move
			Channel(Channel &&) = default;
			Channel &operator=(Channel &&) = default;

			const std::string &GetNameUtf8() const { return name_u8_; }
			const std::u16string &GetName() const { return name_; }
			int GetNumber() const { return number_; }
			int GetSlot() const { return slot_; }
			uint32_t GetFreq() const;

		private:
			static constexpr int32_t FREQUENCY_LIMIT_KHZ = 90000;

			std::string name_u8_;
			std::u16string name_;
			int32_t number_;
			int32_t slot_;
		};

		Space(strutil::CharConv &cv, config::Config::Section &sct);
		Space() = default;
		~Space() = default;

		// cannot copy
		Space(const Space &) = delete;
		Space &operator=(Space &) = delete;

		// can move
		Space(Space &&) = default;
		Space &operator=(Space &&) = default;

		const std::string &GetNameUtf8() const { return name_u8_; }
		const std::u16string &GetName() const { return name_; }
		const std::string GetSystemUtf8() const { return system_u8_; }
		System GetSystem() const { return system_; }
		int32_t GetDemodSequenceState() const { return demod_sequence_state_; }
		int32_t GetMaxErrorPackets() const { return max_error_packets_; }
		int32_t GetTimeoutHasLock() const { return timeout_has_lock_; }
		int32_t GetWaitHasLock() const { return wait_has_lock_; }
		const Channel &GetChannel(std::size_t pos) const { return channel_.at(pos); };
		void AddChannel(strutil::CharConv &cv, config::Config::Section &sct, int32_t max_channels);

	private:
		std::string name_u8_;
		std::u16string name_;
		System system_ = System::UNKNOWN;
		std::string system_u8_;
		int32_t demod_sequence_state_ = 8;
		int32_t max_error_packets_ = 0;
		int32_t timeout_has_lock_ = 3000;
		int32_t wait_has_lock_ = 100;
		std::vector<Channel> channel_;
	};

	struct StreamResult {
		int32_t min_sync_count = 3;	// 初期同期確認を行うパケット数
		bool is_valid_sync = false;	// バッファ末尾まで完全に同期した領域が見つかったか
		int32_t sync_offset = -1;	// 最終的に同期が確定した先頭位置
		int32_t sync_size = 0;		// 最終的に同期が確定したバイト数
		int32_t total_packets = 0;	// 同期領域内の全パケット数
		int32_t error_packets = 0;	// 同期領域内の transport rrror indicator が立っていたパケット数
	};

	void StreamThread();
	int VerifyStream(const uint8_t *data, int32_t size, StreamResult &result);
	

	strutil::CharConv char_conv_{"UTF-8", "UTF-16LE"};

	bool is_stream_thread_running_ = true;
	bool is_stream_thread_paused_ = true;

	std::atomic_int32_t ktv_device_id_ = 0;
	std::atomic_uint32_t current_space_ = 0;
	std::atomic_uint32_t current_channel_ = 0;

	std::u16string name_;
	std::string usb_devfile_;
	std::vector<Space> spaces_;
	std::unique_ptr<EM2874Device> em2874_device_;
	std::unique_ptr<KtvDevice> ktv_device_;

	std::mutex mtx_;
	std::mutex stream_mtx_;
	std::condition_variable stream_cv_;
	std::unique_ptr<std::thread> stream_thread_;
	std::unique_ptr<StreamBuffer> stream_buffer_;
	std::vector<uint8_t> stream_chunk_;

	static std::mutex instance_mtx_;
	static BonDriver *instance_;

	static void DestroyInstance();
};

} // namespace BonDriver_LinuxFSUSB2N
