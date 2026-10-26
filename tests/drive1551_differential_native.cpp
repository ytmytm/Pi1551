#include "Drive1551.h"
#undef DRIVE1551_H
#define Drive ReferenceDrive
#include "reference/Drive1551.h"
#undef Drive

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void SetACTLed(int) {}
static unsigned long long randomCalls = 0;
extern "C" int __real_rand(void);
extern "C" int __wrap_rand(void) {
    ++randomCalls;
    return __real_rand();
}

struct Drive1551DifferentialTest {
    struct Snapshot {
        bool result;
        float cyclesForBit, randomFluxReversalTime;
        unsigned headBitOffset, headTrackPos, bitsInTrack, swapCycles, readShiftRegister;
        int ue7, uf4, ue3, cachedByteOffset;
        unsigned cachedTrack;
        unsigned char cachedByte, writeShiftRegister, portB, portC, portCPU;
        bool motor, led, diskDirty;
    };

    static uint32_t Random(uint32_t& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }

    template <typename T> static void Seed(T& drive, DiskImage& image, m6523& tpi,
                                           uint32_t& random, unsigned scenario) {
        drive.Insert(&image);
        drive.SetTPI(&tpi);
        drive.newDiskImageQueuedCylesRemaining = 0;
        drive.motor = true;
        drive.headTrackPos = 36;
        drive.bitsInTrack = image.BitsInTrack(36);
        unsigned offset = Random(random) % drive.bitsInTrack;
        if (scenario % 4 == 0) offset = drive.bitsInTrack - 1 - scenario % 8;
        drive.headBitOffset = offset;
        drive.cyclesPerBit = scenario % 7 == 0 ? 1.0f :
            scenario % 7 == 1 ? 7.5f : 3200000.0f / drive.bitsInTrack;
        drive.cyclesForBit = (Random(random) % 1000) * drive.cyclesPerBit / 1000.0f;
        drive.randomFluxReversalTime = (Random(random) % 400) / 16.0f;
        drive.CLOCK_SEL_AB = Random(random) % 4;
        drive.UE7Counter = Random(random) % 17;
        drive.UF4Counter = Random(random) % 16;
        drive.UE3Counter = Random(random) % 9;
        drive.readShiftRegister = Random(random);
        drive.writeShiftRegister = Random(random);
        drive.cachedheadTrackPos = static_cast<unsigned>(-1);
        drive.cachedbyteOffset = -1;
        drive.cachedByte = 0;
        tpi.GetPortC()->SetOutput(scenario & 1 ? 0 : 0x10);
        tpi.GetPortB()->SetOutput(Random(random));
    }

    template <typename T> static Snapshot Capture(T& drive, m6523& tpi, bool result) {
        Snapshot s = {result, drive.cyclesForBit, drive.randomFluxReversalTime,
            drive.headBitOffset, drive.headTrackPos, drive.bitsInTrack,
            drive.newDiskImageQueuedCylesRemaining, drive.readShiftRegister, drive.UE7Counter,
            drive.UF4Counter, drive.UE3Counter, drive.cachedbyteOffset,
            drive.cachedheadTrackPos, drive.cachedByte, drive.writeShiftRegister,
            tpi.GetPortB()->GetInput(), tpi.GetPortC()->GetInput(),
            tpi.GetPortCPU()->GetInput(), drive.motor, drive.LED,
            drive.diskImage->IsDirty()};
        return s;
    }

    static bool Equal(const Snapshot& a, const Snapshot& b) {
        return a.result == b.result &&
            std::memcmp(&a.cyclesForBit, &b.cyclesForBit, sizeof(float)) == 0 &&
            std::memcmp(&a.randomFluxReversalTime, &b.randomFluxReversalTime, sizeof(float)) == 0 &&
            a.headBitOffset == b.headBitOffset && a.headTrackPos == b.headTrackPos &&
            a.bitsInTrack == b.bitsInTrack && a.swapCycles == b.swapCycles &&
            a.readShiftRegister == b.readShiftRegister &&
            a.ue7 == b.ue7 && a.uf4 == b.uf4 && a.ue3 == b.ue3 &&
            a.cachedByteOffset == b.cachedByteOffset && a.cachedTrack == b.cachedTrack &&
            a.cachedByte == b.cachedByte && a.writeShiftRegister == b.writeShiftRegister &&
            a.portB == b.portB && a.portC == b.portC && a.portCPU == b.portCPU &&
            a.motor == b.motor && a.led == b.led && a.diskDirty == b.diskDirty;
    }

    template <typename T> static void SeedReal(T& drive, DiskImage& image,
                                               m6523& tpi, unsigned track) {
        tpi.Reset();
        drive.Insert(&image);
        drive.SetTPI(&tpi);
        drive.Reset();
        drive.newDiskImageQueuedCylesRemaining = 0;
        drive.motor = true;
        drive.headTrackPos = track;
        drive.headBitOffset = 0;
        drive.UpdateHeadSectorPosition();
        drive.headBitOffset = drive.bitsInTrack - 1;
        drive.cachedheadTrackPos = static_cast<unsigned>(-1);
        drive.cachedbyteOffset = -1;
        tpi.GetPortC()->SetOutput(0x10);
    }

    static std::vector<unsigned char> ReadFile(const char* path) {
        std::FILE* file = std::fopen(path, "rb");
        if (!file) return {};
        std::fseek(file, 0, SEEK_END);
        long size = std::ftell(file);
        std::rewind(file);
        std::vector<unsigned char> data(size > 0 ? size : 0);
        if (size < 0 || std::fread(data.data(), 1, data.size(), file) != data.size())
            data.clear();
        std::fclose(file);
        return data;
    }

    static int RealDisk(const char* source, const char* output) {
        std::vector<unsigned char> original = ReadFile(source);
        if (original.empty() || original.size() > sizeof(DiskImage::readBuffer)) {
            std::fprintf(stderr, "Cannot read D64: %s\n", source); return 1;
        }
        FILINFO info = {};
        std::snprintf(info.fname, sizeof(info.fname), "%s", output);
        info.fsize = original.size();
        DiskImage referenceImage, currentImage;
        if (!referenceImage.OpenD64(&info, original.data(), original.size()) ||
            !currentImage.OpenD64(&info, original.data(), original.size())) {
            std::fprintf(stderr, "Cannot convert D64 to GCR\n"); return 1;
        }
        ReferenceDrive reference;
        Drive current;
        m6523 referenceTpi, currentTpi;
        unsigned long long totalBits = 0, totalUpdates = 0, totalRandomCalls = 0;
        for (unsigned track = 0; track < 80; track += 2) {
            const unsigned bytes = referenceImage.TrackLength(track);
            if (!bytes || bytes != currentImage.TrackLength(track) ||
                std::memcmp(referenceImage.tracks[track], currentImage.tracks[track], bytes)) {
                std::fprintf(stderr, "GCR conversion mismatch on half-track %u\n", track);
                return 1;
            }
            const unsigned bits = bytes * 8;
            const unsigned seed = 0x15510000u + track;
            std::srand(seed);
            SeedReal(reference, referenceImage, referenceTpi, track);
            unsigned long long referenceRandomStart = randomCalls;
            std::vector<Snapshot> expected;
            std::vector<unsigned char> visited(bits);
            bool moved = false;
            do {
                unsigned previous = reference.headBitOffset;
                bool result = reference.Update(16);
                expected.push_back(Capture(reference, referenceTpi, result));
                if (reference.headBitOffset != previous) {
                    visited[reference.headBitOffset] = 1;
                    moved = true;
                }
                if (expected.size() > 250000) {
                    std::fprintf(stderr, "Read did not finish track %u\n", track); return 1;
                }
            } while (!moved || reference.headBitOffset != bits - 1);
            unsigned long long trackRandomCalls = randomCalls - referenceRandomStart;
            for (unsigned bit = 0; bit < bits; ++bit) {
                if (!visited[bit]) {
                    std::fprintf(stderr, "Unread GCR bit %u on track %u\n", bit, track); return 1;
                }
            }
            std::srand(seed);
            SeedReal(current, currentImage, currentTpi, track);
            unsigned long long currentRandomStart = randomCalls;
            for (unsigned step = 0; step < expected.size(); ++step) {
                Snapshot actual = Capture(current, currentTpi, current.Update(16));
                if (!Equal(expected[step], actual)) {
                    std::fprintf(stderr, "Real D64 mismatch on track %u, update %u\n", track, step);
                    return 1;
                }
            }
            if (std::memcmp(referenceImage.tracks[track], currentImage.tracks[track], bytes)) {
                std::fprintf(stderr, "GCR track changed during read: %u\n", track); return 1;
            }
            if (randomCalls - currentRandomStart != trackRandomCalls) {
                std::fprintf(stderr, "Different random consumption on track %u\n", track); return 1;
            }
            totalBits += bits;
            totalUpdates += expected.size();
            totalRandomCalls += trackRandomCalls;
        }

        // The image name points only at the disposable output path. WriteD64
        // decodes the GCR tracks through the production disk-image code.
        if (!currentImage.WriteD64()) {
            std::fprintf(stderr, "D64 write-back failed\n"); return 1;
        }
        std::vector<unsigned char> roundTrip = ReadFile(output);
        if (roundTrip != original) {
            std::fprintf(stderr, "D64 write-back differs from source (%zu vs %zu bytes)\n",
                         roundTrip.size(), original.size()); return 1;
        }

        // Change payload bytes, regenerate all GCR tracks, and verify that
        // decoding them writes exactly those new bytes to another temp file.
        std::vector<unsigned char> modified = original;
        for (unsigned i = 4096; i < modified.size(); i += 4096) modified[i] ^= 0x5a;
        std::string changedPath = std::string(output) + ".changed.d64";
        std::snprintf(info.fname, sizeof(info.fname), "%s", changedPath.c_str());
        info.fsize = modified.size();
        DiskImage changedImage;
        if (!changedImage.OpenD64(&info, modified.data(), modified.size()) ||
            !changedImage.WriteD64() || ReadFile(changedPath.c_str()) != modified) {
            std::fprintf(stderr, "Modified D64 write-back differs from written data\n"); return 1;
        }
        std::printf("PASS real D64: %llu GCR bits on 40 tracks, %llu matched Drive::Update(16) calls, %llu rand() calls, exact D64 write-back\n",
                    totalBits, totalUpdates, totalRandomCalls);
        return 0;
    }
};

int main(int argc, char** argv) {
    FILINFO info = {};
    std::strcpy(info.fname, "drive-test.d64");
    const unsigned size = DiskImage::CreateNewDiskInRAM("TEST", "00");
    info.fsize = size;
    DiskImage referenceImage, currentImage;
    if (!referenceImage.OpenD64(&info, DiskImage::readBuffer, size) ||
        !currentImage.OpenD64(&info, DiskImage::readBuffer, size)) {
        std::fprintf(stderr, "Cannot create test disk\n"); return 1;
    }
    ReferenceDrive reference;
    Drive current;
    m6523 referenceTpi, currentTpi;
    uint32_t random = 0x1551a55au;
    const unsigned scenarios = 600, steps = 100;
    for (unsigned scenario = 0; scenario < scenarios; ++scenario) {
        unsigned trackBytes = referenceImage.TrackLength(36);
        for (unsigned i = 0; i < trackBytes; ++i) {
            unsigned char value = Drive1551DifferentialTest::Random(random);
            referenceImage.tracks[36][i] = value;
            currentImage.tracks[36][i] = value;
        }
        uint32_t initial = random;
        referenceTpi.Reset();
        currentTpi.Reset();
        Drive1551DifferentialTest::Seed(reference, referenceImage, referenceTpi, random, scenario);
        random = initial;
        Drive1551DifferentialTest::Seed(current, currentImage, currentTpi, random, scenario);
        std::vector<Drive1551DifferentialTest::Snapshot> expected;
        expected.reserve(steps);
        std::vector<unsigned char> expectedTracks(steps * trackBytes);
        const unsigned randSeed = 0x811c9dc5u ^ scenario;
        std::srand(randSeed);
        for (unsigned step = 0; step < steps; ++step) {
            unsigned char mode = (step / 13 + scenario) & 1 ? 0 : 0x10;
            referenceTpi.GetPortC()->SetOutput(mode);
            referenceTpi.GetPortB()->SetOutput((scenario * 29 + step * 37) & 255);
            expected.push_back(Drive1551DifferentialTest::Capture(reference, referenceTpi, reference.Update(16)));
            std::memcpy(&expectedTracks[step * trackBytes], referenceImage.tracks[36], trackBytes);
        }
        std::srand(randSeed);
        for (unsigned step = 0; step < steps; ++step) {
            unsigned char mode = (step / 13 + scenario) & 1 ? 0 : 0x10;
            currentTpi.GetPortC()->SetOutput(mode);
            currentTpi.GetPortB()->SetOutput((scenario * 29 + step * 37) & 255);
            Drive1551DifferentialTest::Snapshot actual = Drive1551DifferentialTest::Capture(current, currentTpi, current.Update(16));
            if (!Drive1551DifferentialTest::Equal(expected[step], actual) ||
                std::memcmp(&expectedTracks[step * trackBytes], currentImage.tracks[36], trackBytes) != 0) {
                std::fprintf(stderr, "Mismatch at scenario %u, update %u, mode %s\n",
                             scenario, step, mode ? "read" : "write");
                return 1;
            }
        }
        if (std::memcmp(referenceImage.tracks[36], currentImage.tracks[36], trackBytes) != 0) {
            std::fprintf(stderr, "Disk bytes differ in scenario %u\n", scenario);
            return 1;
        }
    }
    std::printf("PASS %u Drive::Update(16) calls across %u scenarios; bit-exact state, TPI and disk\n",
                scenarios * steps, scenarios);
    if (argc == 3) return Drive1551DifferentialTest::RealDisk(argv[1], argv[2]);
    return argc == 1 ? 0 : 1;
}
