#pragma once

#include <cstdint>
#include <limits>
#include "components/fs/FS.h"
#include "components/datetime/DateTimeController.h"

namespace Pinetime {
  namespace Controllers {

    class HeartRateHistory {
    public:
      struct Entry {
        uint32_t timestamp;
        uint8_t avgBpm;
        uint8_t minBpm;
        uint8_t maxBpm;
        uint8_t flags;
      };

      static constexpr uint16_t maxEntries = 576;
      static constexpr uint16_t bucketSeconds = 300;
      static constexpr uint8_t flagValid = 0x01;

      HeartRateHistory(Controllers::FS& fs, Controllers::DateTime& dateTime);

      void Accumulate(uint8_t bpm);
      void TryFlush();

      Entry GetEntry(uint16_t index);
      uint16_t EntryCount() const;

      void Load();
      void Save();

    private:
      struct Header {
        uint32_t version;
        uint16_t writeIndex;
        uint16_t count;
      };

      static constexpr uint32_t currentVersion = 1;
      static constexpr const char* filePath = "/hr_history.dat";
      static constexpr uint32_t headerSize = sizeof(Header);
      static constexpr uint32_t entrySize = sizeof(Entry);

      uint32_t currentBucketId = 0;
      uint32_t bpmSum = 0;
      uint16_t bpmCount = 0;
      uint8_t bpmMin = std::numeric_limits<uint8_t>::max();
      uint8_t bpmMax = 0;

      Header header = {currentVersion, 0, 0};
      Controllers::FS& fs;
      Controllers::DateTime& dateTime;

      void FlushBucket();
      void WriteEntry(const Entry& entry);
      Entry ReadEntry(uint16_t ringIndex);
      uint32_t CurrentBucketId();
    };

  }
}
