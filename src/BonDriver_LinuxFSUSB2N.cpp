// SPDX-License-Identifier: MIT
/*
 * BonDriver for FSUSB2N (BonDriver_LinuxFSUSB2N.cpp)
 *
 * Copyright (c) 2026 hendecarows
 */

#include "BonDriver_LinuxFSUSB2N.hpp"

#include "config.hpp"
#include "runtime_error.hpp"
#include "strutil.hpp"

#include <cerrno>
#include <unistd.h>
#include <dlfcn.h>

#include <cstddef>
#include <cstdint>

#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <plog/Formatters/FuncMessageFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Log.h>

namespace BonDriver_LinuxFSUSB2N {

BonDriver::BonDriver(config::Config &config)
{
	PLOGV << __func__;

	try {
		// INIファイルの読み込み
		// [BonDriver_LinuxFSUSB2N]
		auto sct = config.Get("BonDriver_LinuxFSUSB2N");
		auto loglevel = sct.GetInt("LogLevel", 4);
		if (!plog::get()) {
			plog::init<plog::FuncMessageFormatter>(static_cast<plog::Severity>(loglevel), plog::streamStdErr);
			PLOGD << "plog init = " << plog::get()->getInstance();
		} else {
			plog::get()->setMaxSeverity(static_cast<plog::Severity>(loglevel));
		}

		auto name = sct.GetStr("Name", "LinuxFSUSB2N");
		name_ = char_conv_.Convert<std::u16string>(name);

		// USB デバイスの指定
		// (1)udevルールファイルで作成したデバイスファイルを指定
		// Device=/dev/fsusb2n0
		usb_devfile_ = sct.GetStr("Device", "");
		PLOGD << "Device = " << usb_devfile_;

		// ストリームバッファサイズ
		auto stream_buffer_size =
			sct.GetUIntMinMax("StreamBufferSize", 4 * 1024 * 1024, 2 * 1024 * 1024, 20 * 1024 * 1024);
		PLOGD << "StreamBufferSize = " << stream_buffer_size;

		// TSパケットを一度に読み出すサイズ
		auto ts_packets_per_chunk = sct.GetIntMinMax("TsPacketsPerChunk", 2048, 100, 30000);
		PLOGD << "TsPacketsPerChunk = " << ts_packets_per_chunk;

		// チャンネルデータ最大数
		auto max_channels = sct.GetIntMinMax("MaximumNumberOfChannels", 300, 300, 1000);
		PLOGD << "MaximumNumberOfChannels = " << max_channels;

		// ストリームバッファの確保
		stream_buffer_ = std::make_unique<StreamBuffer>(stream_buffer_size);
		stream_chunk_.resize(ts_packets_per_chunk * TS_PACKET_SIZE);

		// チャンネル設定
		auto spaces = strutil::Split(config.Get("Space").Get("Space"));
		for (auto v : spaces) {
			v = "Space." + v;
			PLOGD << v;
			auto subspace_sct = config.Get(v);
			auto subspace_ch_sct = config.Get(v + ".Channel");
			auto& space = spaces_.emplace_back(char_conv_, subspace_sct);
			space.AddChannel(char_conv_, subspace_ch_sct, max_channels);
		}
	} catch (const std::exception &e) {
		PLOGE << e.what();
		throw;
	}
}

BonDriver::~BonDriver()
{
	PLOGV << __func__;

	CloseTuner();
}

bool BonDriver::OpenTuner(void)
{
	PLOGV << __func__;

	using namespace std::chrono_literals;

	std::lock_guard<std::mutex> lock(mtx_);

	if (ktv_device_id_) {
		return true;
	}

	try {
		em2874_device_ = std::make_unique<EM2874Device>();
		if (!em2874_device_->openDevice(usb_devfile_)) {
			PLOGE << "failed to open EM2874Device: " << usb_devfile_;
			return false;
		}

		// チューナー種類を取得
		std::this_thread::sleep_for(80ms);
		ktv_device_id_ = em2874_device_->getDeviceID();
		PLOGD << "em2874_device getDeviceID: " << ktv_device_id_;

		switch (ktv_device_id_) {
		case 1:
			// チューナー対応周波数
			// TDA18211 : 174MHz - 864MHz
			PLOGD << "em2874_device: Ktv1Device";
			ktv_device_ = std::make_unique<Ktv1Device>(em2874_device_.get());
			break;
		case 2:
			// チューナー対応周波数
			// MxL135RF : 44MHz - 885MHz
			PLOGD << "em2874_device: Ktv2Device";
			ktv_device_ = std::make_unique<Ktv2Device>(em2874_device_.get());
			break;
		default:
			PLOGE << "unsupported ktv_device id: " << ktv_device_id_;
			return false;
		}
		
		// チューナーの初期化
		PLOGD << "ktv_device: InitTuner";
		std::this_thread::sleep_for(160ms);
		ktv_device_->InitTuner();

		// 復調デバイスの初期化
		PLOGD << "ktv_device: InitDeMod, ResetDeMod";
		std::this_thread::sleep_for(180ms);
		ktv_device_->InitDeMod();
		ktv_device_->ResetDeMod();

		// TSスレッドを一時停止で起動
		PLOGD << "stream thread: start";
		is_stream_thread_running_ = true;
		is_stream_thread_paused_ = true;
		stream_thread_ = std::make_unique<std::thread>(&BonDriver::StreamThread, this);
	} catch (const std::exception &e) {
		PLOGE << e.what();
		return false;
	}

	return true;
}

void BonDriver::CloseTuner(void)
{
	PLOGV << __func__;

	std::lock_guard<std::mutex> lock(mtx_);

	if (!ktv_device_id_) {
		return;
	}

	{
		PLOGD << "stream thread: wake up";
		std::lock_guard<std::mutex> lock(stream_mtx_);
		is_stream_thread_running_ = false;
		is_stream_thread_paused_ = false;
	}
	stream_cv_.notify_all();
	
	if (stream_thread_ && stream_thread_->joinable()) {
		PLOGD << "stream thread: join and destroy";
		stream_thread_->join();
		stream_thread_.reset();
	}

	if (em2874_device_) {
		PLOGD << "em2874_device: stopStream";
		em2874_device_->stopStream();
	}

	if (ktv_device_) {
		PLOGD << "ktv_device: destroy";
		ktv_device_.reset();
	}

	if (em2874_device_) {
		PLOGD << "em2874_device: destroy";
		em2874_device_.reset();
	}

	if (stream_buffer_) {
		PLOGD << "stream buffer: destroy";
		stream_buffer_.reset();
	}

	current_space_.store(0, std::memory_order_release);
	current_channel_.store(0, std::memory_order_release);
	ktv_device_id_ = 0;
}

bool BonDriver::SetChannel(const uint8_t bCh)
{
	return SetChannel(0, bCh);
}

float BonDriver::GetSignalLevel(void)
{
	if (!ktv_device_id_) {
		return 0.0f;
	}

	auto cnr = 0.01f * ktv_device_->DeMod_GetQuality();
	if (cnr < 0.0f) {
		cnr = 0.0f;
	}

	return cnr;
}

uint32_t BonDriver::WaitTsStream(const uint32_t /*dwTimeOut*/)
{
	if (!ktv_device_id_) {
		return WAIT_ABANDONED;
	} else if (!stream_buffer_->UsedSpace()) {
		return WAIT_OBJECT_0;
	} else {
		return WAIT_TIMEOUT;
	}
}

uint32_t BonDriver::GetReadyCount(void)
{
	return stream_buffer_->UsedSpace() ? 1 : 0;
}

bool BonDriver::GetTsStream(uint8_t *pDst, uint32_t *pdwSize, uint32_t *pdwRemain)
{
	if (!pDst || !pdwSize) {
		return false;
	}

	uint8_t *psrc = nullptr;
	if (GetTsStream(&psrc, pdwSize, pdwRemain)) {
		if (*pdwSize > 0) {
			std::copy(psrc, psrc + *pdwSize, pDst);
		}
		return true;
	}

	return false;
}

bool BonDriver::GetTsStream(uint8_t **ppDst, uint32_t *pdwSize, uint32_t *pdwRemain)
{
	if (!ppDst || !pdwSize) {
		return false;
	}

	*ppDst = nullptr;
	*pdwSize = 0;

	auto size = stream_buffer_->Read(stream_chunk_.data(), stream_chunk_.size());
	if (size > 0) {
		*ppDst = stream_chunk_.data();
		*pdwSize = static_cast<uint32_t>(size);
	}

	if (pdwRemain) {
		*pdwRemain = static_cast<uint32_t>(stream_buffer_->UsedSpace());
	}

	return true;
}

void BonDriver::PurgeTsStream(void)
{
	PLOGV << __func__;
	stream_buffer_->Clear();
}

void BonDriver::Release(void)
{
	PLOGV << __func__;
	DestroyInstance();
}

const char16_t *BonDriver::GetTunerName(void)
{
	return name_.c_str();
}

bool BonDriver::IsTunerOpening(void)
{
	return ktv_device_id_ != 0 ? true : false;
}

const char16_t *BonDriver::EnumTuningSpace(const uint32_t dwSpace)
{
	try {
		return spaces_.at(dwSpace).GetName().c_str();
	} catch (const std::out_of_range&) {
		return nullptr;
	}
}

const char16_t *BonDriver::EnumChannelName(const uint32_t dwSpace, const uint32_t dwChannel)
{
	try {
		return spaces_.at(dwSpace).GetChannel(dwChannel).GetName().c_str();
	} catch (const std::out_of_range&) {
		return nullptr;
	}
}

bool BonDriver::SetChannel(const uint32_t dwSpace, const uint32_t dwChannel)
{
	PLOGV << __func__;

	using namespace std::chrono_literals;

	std::lock_guard<std::mutex> lock(mtx_);

	if (!ktv_device_id_) {
		return false;
	}

	uint32_t freq = 0;
	auto demod_squence_state = 9;
	auto timeout_has_lock = 3000;
	auto wait_has_lock = 100;

	try {
		auto& s = spaces_.at(dwSpace);
		if (s.GetSystem() != System::ISDB_T) {
			PLOGE << "unsupported system = " << s.GetSystemUtf8();
			return false;
		}

		freq = s.GetChannel(dwChannel).GetFreq();
		demod_squence_state = s.GetDemodSequenceState();
		timeout_has_lock = s.GetTimeoutHasLock();
		wait_has_lock = s.GetWaitHasLock();

		PLOGD << "GetChannel space = " << dwSpace
			<< " channel = " << dwChannel
			<< " freq = " << freq;

		// チューナー対応周波数
		switch (ktv_device_id_) {
		case 1:
			// TDA18211 : 174MHz - 864MHz
			if (freq < 174000 || freq > 864000) {
				throw RuntimeError(std::format("frequency error TDA18211 174MHz - 864MHz  f = {}", freq));
			}
			break;
		case 2:
			// MxL135RF : 44MHz - 885MHz
			if (freq < 44000 || freq > 885000) {
				throw RuntimeError(std::format("frequency error MxL135RF 44MHz - 885MHz f = {}", freq));
				return false;
			}
			break;
		default:
			throw RuntimeError(std::format("unknown ktv_device_id = {}", ktv_device_id_.load()));
		}
	} catch (const std::exception &e) {
		PLOGE << e.what();
		return false;
	}

	// TS受信スレッドの一時停止
	{
		PLOGD << "pause stream thread";
		std::lock_guard<std::mutex> lock(stream_mtx_);
		is_stream_thread_paused_ = true;
	}

	// em2874TS出力の一時停止
	if (em2874_device_->isStreaming()) {
		PLOGD << "em2874_device: pauseStream";
		em2874_device_->pauseStream();
	}

	PLOGD << "ktv_device: SetFrequency";
	ktv_device_->SetFrequency(freq);

	PLOGD << "ktv_device: ResetDmod";
	std::this_thread::sleep_for(5ms);
	ktv_device_->ResetDeMod();

	if (em2874_device_->isStreaming()) {
		PLOGD << "em2874_device: resumeStream";
		em2874_device_->resumeStream();
	} else {
		PLOGD << "em2874_device: startStream";
		em2874_device_->startStream();
	}

	auto start = std::chrono::steady_clock::now();
	auto now = start;
	auto timeout = now + std::chrono::milliseconds(timeout_has_lock);
	auto wait = std::chrono::milliseconds(wait_has_lock);
	auto is_locked = false;
	uint8_t *data = nullptr;
	StreamResult result;
	while (now <= timeout) {
		auto sequence = static_cast<int32_t>(ktv_device_->DeMod_GetSequenceState());
		auto size = em2874_device_->getStream((const void **)&data);
		PLOGD << "elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
			<< " demod sequence: " << sequence
			<< " stream size: " << size;

		VerifyStream(data, size, result);
		if (sequence >= demod_squence_state && result.is_valid_sync && result.error_packets == 0) {
			PLOGD << "has lock demod sequence: " << sequence;
			is_locked = true;
			break;
		}

		std::this_thread::sleep_for(wait);
		now = std::chrono::steady_clock::now();
	}

	if (is_locked) {
		PurgeTsStream();
		size_t lost_size = 0;
		stream_buffer_->Write(data + result.sync_offset, result.sync_size, lost_size);
		PLOGD << "lost_size: " << lost_size;
	} else {
		PLOGE << "failed to lock freq: " << freq;
		em2874_device_->stopStream();
		PurgeTsStream();
		return false;
	}

	// TS受信スレッドの再開
	{
		std::lock_guard<std::mutex> lock(stream_mtx_);
		is_stream_thread_paused_ = false;
	}
	stream_cv_.notify_one();

	current_space_.store(dwSpace, std::memory_order_release);
	current_channel_.store(dwChannel, std::memory_order_release);

	return true;
}

uint32_t BonDriver::GetCurSpace(void)
{
	return current_space_.load(std::memory_order_acquire);
}

uint32_t BonDriver::GetCurChannel(void)
{
	return current_channel_.load(std::memory_order_acquire);
}

void BonDriver::StreamThread()
{
	PLOGV << __func__;

	if (!ktv_device_id_) {
		PLOGE << "no open tuner";
		return;
	}

	uint8_t *data = nullptr;
	while (is_stream_thread_running_) {
		{
			// is_stream_thread_paused_ が true の間はスレッドを停止
			std::unique_lock<std::mutex> lock(stream_mtx_);
			stream_cv_.wait(lock, [this] { 
				return !is_stream_thread_paused_ || !is_stream_thread_running_; 
			});
		}

		if (!is_stream_thread_running_) break;

		auto size = em2874_device_->getStream((const void **)&data);
		if (size > 0) {
			size_t lost_data = 0;
			stream_buffer_->Write(data,  size, lost_data);
			if (lost_data > 0) {
				PLOGW << "stream buffer overflow: " << lost_data << " byte lost";
			}
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

	PLOGD << "stream thread: exit";
}

int BonDriver::VerifyStream(const uint8_t *data, int32_t size, StreamResult &result)
{
	PLOGV << __func__;

	auto min_sync_count = result.min_sync_count;

	if (!data || size < TS_PACKET_SIZE * min_sync_count) {
		result.is_valid_sync = false;
		result.sync_offset = -1;
		result.sync_size = 0;
		result.total_packets = 0;
		result.error_packets = 0;
		return -EINVAL;
	}

	auto offset = 0;
	auto is_valid_sync = false;
	auto sync_offset = -1;
	auto total_packets = 0;
	auto error_packets = 0;
	while (offset + (TS_PACKET_SIZE * min_sync_count) <= size) {
		// 先頭の 0x47 を探す
		if (data[offset] != TS_SYNC_BYTE) {
			offset++;
			continue;
		}

		// 指定数(min_sync_count)だけ 188バイト周期で 0x47 が続くか初期検証
		auto is_initial_sync = true;
		for (auto i = 1; i < min_sync_count; ++i) {
			if (data[offset + i * TS_PACKET_SIZE] != TS_SYNC_BYTE) {
				is_initial_sync = false;
				break;
			}
		}

		// 初期同期が確認できない場合は、次バイトから再度 0x47 を探す
		if (!is_initial_sync) {
			offset++;
			continue;
		}

		// 初期同期が確認できた場合、バッファの最後までの同期確認
		is_valid_sync = true;
		sync_offset = offset;
		total_packets = 0;
		error_packets = 0;
		auto current_pos = offset;
		while (current_pos + TS_PACKET_SIZE <= size) {
			const uint8_t* pkt = &data[current_pos];

			// 全パケットの 0x47 同期チェック
			if (pkt[0] == TS_SYNC_BYTE) {
				total_packets++;

				// transport_error_indicator (2バイト目の最上位ビット) の確認
				bool tei = (pkt[1] & 0x80) != 0;
				if (tei) {
					error_packets++;
				}
			} else {
				// 途中で同期が壊れた場合
				PLOGD << "TS sync lost at offset: " << current_pos;
				is_valid_sync = false;
				break; 
			}

			current_pos += TS_PACKET_SIZE;
		}

		if (is_valid_sync) {
			// バッファの最後まで同期が確認できた場合
			break;
		} else {
			// バッファの途中で同期が壊れた場合
			offset = current_pos + 1;
			sync_offset = -1;
			total_packets = 0;
			error_packets = 0;
		}
	}

	if (is_valid_sync) {
		result.is_valid_sync = is_valid_sync;
		result.sync_offset = sync_offset;
		result.sync_size = size - sync_offset;
		result.total_packets = total_packets;
		result.error_packets = error_packets;
	} else {
		result.is_valid_sync = false;
		result.sync_offset = -1;
		result.sync_size = 0;
		result.total_packets = 0;
		result.error_packets = 0;
	}

	PLOGD << "sync = " << result.is_valid_sync
		<< " offset = " << result.sync_offset
		<< " size = " << result.sync_size
		<< " total = " << result.total_packets
		<< " error = " << result.error_packets;

	return 0;
}

BonDriver::Space::Channel::Channel(const std::string& name_u8, const std::u16string& name, int32_t number, int32_t slot) :
	name_u8_(name_u8), name_(name), number_(number), slot_(slot)
{
}

uint32_t BonDriver::Space::Channel::GetFreq() const
{
	uint32_t freq = 0;
	if ((number_ >= 3 && number_ <= 12) ||
		(number_ >= 22 && number_ <= 62)) {
		// CATV C13-C22ch, C23-C63ch
		freq = 93143 + number_ * 6000 + slot_;

		if (number_ == 12)
			freq += 2000;
	} else if (number_ >= 63 && number_ <= 112) {
		/* UHF 13-62ch */
		freq = 95143 + number_ * 6000 + slot_;
	} else if (number_ > FREQUENCY_LIMIT_KHZ) {
		freq = number_;
	}

	return freq;
}

BonDriver::Space::Space(strutil::CharConv &cv, config::Config::Section& sct)
{
	name_u8_ = sct.Get("Name");
	name_ = cv.Convert<std::u16string>(name_u8_);
	system_u8_ = sct.Get("System");
	demod_sequence_state_ = sct.GetIntMinMax("DemodSequenceState", 9, 7, 9);
	timeout_has_lock_ = sct.GetIntMinMax("TimeoutHasLock", 3000, 0, 5000);
	wait_has_lock_ = sct.GetIntMinMax("WaitHasLock", 100, 10, 1000);

	if (!system_u8_.compare("ISDB-T")) {
		system_ = System::ISDB_T;
	} else {
		throw RuntimeError(std::format("unsupported system: {}", system_u8_));
	}
}

void BonDriver::Space::AddChannel(strutil::CharConv& cv, config::Config::Section& sct, int32_t max_channels)
{
	for (auto i = 0; i < max_channels; i++) {
		auto ch = std::format("Ch{}", i);
		try {
			auto str = sct.GetStr(ch, "");
			if (str.empty()) {
				PLOGD << ch << " is undefined and exit";
				break;
			}

			auto data = strutil::Split(str);
			if (data.size() != 3) {
				PLOGD << ch << '=' << str << " data size mismatch and exit";
				break;
			}

			auto name = cv.Convert<std::u16string>(data[0]);
			auto number = std::stoi(data[1], nullptr, 0);
			auto slot = std::stoi(data[2], nullptr, 0);
			channel_.emplace_back(data[0], name, number, slot);
		} catch (const std::exception& e) {
			PLOGE << e.what();
			break;
		}
	}
}


std::mutex BonDriver::instance_mtx_;
BonDriver *BonDriver::instance_ = nullptr;

BonDriver *BonDriver::GetInstance()
{
	std::lock_guard<std::mutex> lock(instance_mtx_);

	if (!instance_) {
		try {
			Dl_info dli;
			if (!::dladdr(reinterpret_cast<void *>(GetInstance), &dli)) {
				return nullptr;
			}

			std::filesystem::path p = dli.dli_fname;
			if (p.stem().empty() || p.extension() != ".so") {
				return nullptr;
			}

			// 以下の順で読み込む
			// (1)BonDriver_LinuxFSUSB2N.ini
			// (2)BonDriver_LinuxFSUSB2N.so.ini
			config::Config config;
			if (!config.Load(p.replace_extension(".ini"))) {
				if (!config.Load(p.replace_extension(".so.ini"))) {
					return nullptr;
				}
			}

			instance_ = new BonDriver(config);
		} catch (...) {
			return nullptr;
		}
	}

	return instance_;
}

void BonDriver::DestroyInstance()
{
	std::lock_guard<std::mutex> lock(instance_mtx_);

	if (instance_) {
		delete instance_;
		instance_ = nullptr;
	}

	return;
}

extern "C" IBonDriver *CreateBonDriver()
{
	return BonDriver::GetInstance();
}

} // namespace BonDriver_LinuxFSUSB2N
