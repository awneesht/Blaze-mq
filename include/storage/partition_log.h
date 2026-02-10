#pragma once
#include "core/types.h"
#include "utils/endian.h"
#include "utils/memory_pool.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace blaze {
namespace storage {

class LogSegment {
public:
    LogSegment(const std::filesystem::path& path, int64_t base_offset, uint64_t max_size)
        : path_(path), base_offset_(base_offset), max_size_(max_size), write_offset_(0)
    {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) throw std::runtime_error("Failed to open segment: " + path.string());
        if (::ftruncate(fd_, static_cast<off_t>(max_size)) != 0)
            throw std::runtime_error("Failed to pre-allocate segment");

        data_ = static_cast<uint8_t*>(::mmap(nullptr, max_size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (data_ == MAP_FAILED) throw std::runtime_error("Failed to mmap segment");

#ifdef BLAZE_PLATFORM_LINUX
        ::madvise(data_, max_size, MADV_HUGEPAGE);
        ::posix_fadvise(fd_, 0, max_size, POSIX_FADV_SEQUENTIAL);
#endif
    }

    ~LogSegment() {
        if (data_ && data_ != MAP_FAILED) {
            ::msync(data_, write_offset_.load(), MS_ASYNC);
            ::munmap(data_, max_size_);
        }
        if (fd_ >= 0) ::close(fd_);
    }

    [[nodiscard]] int64_t append(const uint8_t* data, uint32_t length) noexcept {
        uint64_t offset = write_offset_.load(std::memory_order_relaxed);
        if (offset + length > max_size_) return -1;
        std::memcpy(data_ + offset, data, length);
        write_offset_.store(offset + length, std::memory_order_release);
        return static_cast<int64_t>(offset);
    }

    [[nodiscard]] const uint8_t* read(uint64_t offset, uint32_t length) const noexcept {
        if (offset + length > write_offset_.load(std::memory_order_acquire)) return nullptr;
        return data_ + offset;
    }

    void sync() { ::msync(data_, write_offset_.load(std::memory_order_relaxed), MS_SYNC); }

    [[nodiscard]] int64_t base_offset() const noexcept { return base_offset_; }
    [[nodiscard]] uint64_t size() const noexcept { return write_offset_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool is_full() const noexcept { return size() >= static_cast<uint64_t>(max_size_ * 0.95); }

private:
    std::filesystem::path path_;
    int fd_ = -1;
    uint8_t* data_ = nullptr;
    int64_t base_offset_;
    uint64_t max_size_;
    std::atomic<uint64_t> write_offset_;
};

struct OffsetIndex {
    struct Entry { int64_t offset; uint64_t position; };
    std::vector<Entry> entries;
    [[nodiscard]] uint64_t lookup(int64_t target) const {
        if (entries.empty()) return 0;
        auto it = std::lower_bound(entries.begin(), entries.end(), target,
            [](const Entry& e, int64_t o) { return e.offset < o; });
        if (it == entries.begin()) return entries[0].position;
        --it; return it->position;
    }
    void add(int64_t offset, uint64_t position) { entries.push_back({offset, position}); }
};

class PartitionLog {
public:
    PartitionLog(const TopicPartition& tp, const std::string& base_dir, uint64_t segment_size)
        : tp_(tp)
        , base_dir_(base_dir + "/" + tp.topic + "-" + std::to_string(tp.partition))
        , segment_size_(segment_size), next_offset_(0)
    {
        std::filesystem::create_directories(base_dir_);
        roll_segment();
    }

    [[nodiscard]] int64_t append(RecordBatch& batch) {
        int64_t base_offset = next_offset_.load(std::memory_order_relaxed);
        batch.base_offset = base_offset;
        for (size_t i = 0; i < batch.records.size(); ++i)
            batch.records[i].offset = base_offset + static_cast<int64_t>(i);
        batch.last_offset = base_offset + static_cast<int64_t>(batch.records.size()) - 1;

        uint8_t buf[65536];
        uint32_t sz = serialize_batch(batch, buf, sizeof(buf));

        int64_t byte_off = active_segment_->append(buf, sz);
        if (byte_off < 0) { roll_segment(); byte_off = active_segment_->append(buf, sz); }
        if (byte_off % 4096 == 0) index_.add(base_offset, static_cast<uint64_t>(byte_off));

        next_offset_.store(batch.last_offset + 1, std::memory_order_release);
        return base_offset;
    }

    [[nodiscard]] std::vector<RecordBatch> read(int64_t start, int32_t max_bytes) const {
        std::vector<RecordBatch> result;
        RecordBatch b; b.base_offset = start; b.topic_partition = tp_;
        result.push_back(std::move(b));
        return result;
    }

    [[nodiscard]] int64_t next_offset() const { return next_offset_.load(std::memory_order_acquire); }
    [[nodiscard]] int64_t start_offset() const { return segments_.empty() ? 0 : segments_.front()->base_offset(); }
    [[nodiscard]] int64_t high_watermark() const { return next_offset() - 1; }
    void sync() { if (active_segment_) active_segment_->sync(); }

private:
    void roll_segment() {
        int64_t off = next_offset_.load(std::memory_order_relaxed);
        auto path = std::filesystem::path(base_dir_) / (std::to_string(off) + ".log");
        auto seg = std::make_unique<LogSegment>(path, off, segment_size_);
        active_segment_ = seg.get();
        segments_.push_back(std::move(seg));
    }

    static uint32_t serialize_batch(const RecordBatch& batch, uint8_t* buf, uint32_t cap) {
        uint32_t pos = 0;

        // Use blaze::bswap* from utils/endian.h (no protocol:: dependency)
        auto w64 = [&](int64_t v) {
            uint64_t be = blaze::bswap64(static_cast<uint64_t>(v));
            std::memcpy(buf + pos, &be, 8); pos += 8;
        };
        auto w32 = [&](int32_t v) {
            uint32_t be = blaze::bswap32(static_cast<uint32_t>(v));
            std::memcpy(buf + pos, &be, 4); pos += 4;
        };

        w64(batch.base_offset);
        w32(0); // batchLength placeholder
        w32(batch.partition_leader_epoch);
        buf[pos++] = static_cast<uint8_t>(batch.magic);
        w32(0); // CRC placeholder
        w32(static_cast<int32_t>(batch.attributes));
        w32(static_cast<int32_t>(batch.records.size()) - 1); // lastOffsetDelta
        w64(batch.first_timestamp);
        w64(batch.max_timestamp);
        w64(batch.producer_id);
        w32(static_cast<int32_t>(batch.producer_epoch));
        w32(batch.base_sequence);
        w32(static_cast<int32_t>(batch.records.size()));

        for (const auto& r : batch.records) {
            if (r.data && r.total_size > 0 && pos + r.total_size < cap) {
                std::memcpy(buf + pos, r.data, r.total_size);
                pos += r.total_size;
            }
        }

        // Patch batch length (offset 8, length = total - 12 for baseOffset+batchLength)
        uint32_t bl = pos - 12;
        uint32_t bel = blaze::bswap32(bl);
        std::memcpy(buf + 8, &bel, 4);

        return pos;
    }

    TopicPartition tp_;
    std::string base_dir_;
    uint64_t segment_size_;
    std::atomic<int64_t> next_offset_;
    std::vector<std::unique_ptr<LogSegment>> segments_;
    LogSegment* active_segment_ = nullptr;
    OffsetIndex index_;
};

} // namespace storage
} // namespace blaze
