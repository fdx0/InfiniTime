#include "components/heartrate/HeartRateHistory.h"
#include <cstring>
#include <chrono>

using namespace Pinetime::Controllers;

HeartRateHistory::HeartRateHistory(Controllers::FS& fs, Controllers::DateTime& dateTime)
  : fs {fs}, dateTime {dateTime} {
}

uint32_t HeartRateHistory::CurrentBucketId() {
  auto now = dateTime.CurrentDateTime();
  auto epoch = std::chrono::system_clock::to_time_t(
    std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
  if (epoch <= 0) {
    return 0;
  }
  return static_cast<uint32_t>(epoch) / bucketSeconds;
}

void HeartRateHistory::Accumulate(uint8_t bpm) {
  if (bpm == 0) {
    return;
  }

  uint32_t bucketId = CurrentBucketId();
  if (bucketId == 0) {
    return;
  }

  if (currentBucketId == 0) {
    currentBucketId = bucketId;
  }

  if (bucketId > currentBucketId) {
    FlushBucket();
    currentBucketId = bucketId;
  }

  bpmSum += bpm;
  bpmCount++;
  if (bpm < bpmMin) {
    bpmMin = bpm;
  }
  if (bpm > bpmMax) {
    bpmMax = bpm;
  }
}

void HeartRateHistory::TryFlush() {
  uint32_t bucketId = CurrentBucketId();
  if (bucketId == 0 || currentBucketId == 0) {
    return;
  }
  if (bucketId > currentBucketId) {
    FlushBucket();
    currentBucketId = bucketId;
  }
}

void HeartRateHistory::FlushBucket() {
  Entry entry {};
  entry.timestamp = currentBucketId * bucketSeconds;

  if (bpmCount > 0) {
    entry.avgBpm = static_cast<uint8_t>(bpmSum / bpmCount);
    entry.minBpm = bpmMin;
    entry.maxBpm = bpmMax;
    entry.flags = flagValid;
  } else {
    entry.avgBpm = 0;
    entry.minBpm = 0;
    entry.maxBpm = 0;
    entry.flags = 0;
  }

  WriteEntry(entry);

  bpmSum = 0;
  bpmCount = 0;
  bpmMin = std::numeric_limits<uint8_t>::max();
  bpmMax = 0;
}

void HeartRateHistory::WriteEntry(const Entry& entry) {
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_WRONLY | LFS_O_CREAT) != LFS_ERR_OK) {
    return;
  }

  uint32_t offset = headerSize + header.writeIndex * entrySize;
  fs.FileSeek(&file, offset);
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&entry), entrySize);

  header.writeIndex = (header.writeIndex + 1) % maxEntries;
  if (header.count < maxEntries) {
    header.count++;
  }

  fs.FileSeek(&file, 0);
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), headerSize);

  fs.FileClose(&file);
}

HeartRateHistory::Entry HeartRateHistory::ReadEntry(uint16_t ringIndex) {
  Entry entry {};
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return entry;
  }

  uint32_t offset = headerSize + ringIndex * entrySize;
  fs.FileSeek(&file, offset);
  fs.FileRead(&file, reinterpret_cast<uint8_t*>(&entry), entrySize);
  fs.FileClose(&file);
  return entry;
}

HeartRateHistory::Entry HeartRateHistory::GetEntry(uint16_t index) {
  if (index >= header.count) {
    Entry empty {};
    return empty;
  }

  uint16_t ringIndex;
  if (header.writeIndex >= index + 1) {
    ringIndex = header.writeIndex - index - 1;
  } else {
    ringIndex = maxEntries - (index + 1 - header.writeIndex);
  }

  return ReadEntry(ringIndex);
}

uint16_t HeartRateHistory::EntryCount() const {
  return header.count;
}

void HeartRateHistory::Load() {
  lfs_file_t file;
  if (fs.FileOpen(&file, filePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return;
  }

  Header loadedHeader {};
  fs.FileRead(&file, reinterpret_cast<uint8_t*>(&loadedHeader), headerSize);
  fs.FileClose(&file);

  if (loadedHeader.version == currentVersion &&
      loadedHeader.writeIndex < maxEntries &&
      loadedHeader.count <= maxEntries) {
    header = loadedHeader;
  }
}

void HeartRateHistory::Save() {
  if (bpmCount > 0) {
    FlushBucket();
    currentBucketId = CurrentBucketId();
  }
}
