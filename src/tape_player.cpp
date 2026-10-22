// Pi1551 - TAP cassette image playback and recording
// Copyright(C) 2024-2026 Maciej Witkowiak
//
// This file is part of Pi1541.
// 
// Pi1541 is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Pi1541 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Pi1541. If not, see <http://www.gnu.org/licenses/>.

#include "tape_player.h"
#include "rpiHardware.h"
#include "options.h"
#include "OledMenu.h"
#include "Petscii.h"
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include "Pi1551Platform.h"
#if !defined(PI1551_NATIVE_TEST) || defined(PI1551_NATIVE_GPIO_IRQ_TEST)
#include "interrupt.h"
#endif
#include <limits>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

#define TAPE_CLOCK_PAL_HZ  886724
#define TAPE_CLOCK_NTSC_HZ 894886

#if defined(PI1551_NATIVE_GPIO_IRQ_TEST)
extern u32 NativeTapeIRQRead(u32 address);
extern void NativeTapeIRQWrite(u32 address, u32 value);
static inline u32 TapeIRQRead(u32 address) { return NativeTapeIRQRead(address); }
static inline void TapeIRQWrite(u32 address, u32 value) { NativeTapeIRQWrite(address, value); }
#else
static inline u32 TapeIRQRead(u32 address) { return read32(address); }
static inline void TapeIRQWrite(u32 address, u32 value) { write32(address, value); }
#endif

extern "C"
{
#include "rpi-aux.h"
}

// Options live in main.cpp; treated as read-only configuration after startup.
extern Options options;

// Static member definitions
u64* TapePlayer::pulseTimings = nullptr;
size_t TapePlayer::pulseCount = 0;
size_t TapePlayer::pulseCapacity = 0;
size_t TapePlayer::currentPulseIndex = 0;
volatile bool TapePlayer::loaded = false;
volatile bool TapePlayer::playing = false;
volatile bool TapePlayer::recording = false;
bool TapePlayer::unsaved = false;
bool TapePlayer::recordMotor = false;
bool TapePlayer::recordWrite = false;
u32 TapePlayer::recordEdgeTime = 0;
u8 TapePlayer::tapeVideo = 0;
u8 TapePlayer::tapVersion = 0;
u32 TapePlayer::tapDataSize = 0;
volatile bool TapePlayer::motorActive = true;
volatile bool TapePlayer::motorAlwaysOn = true;
volatile bool TapePlayer::atEnd = false;
bool TapePlayer::readLineState = true;  // Start with READ line high (hardware inverts GPIO signal) - matches cbmtapepi initial state, first transfer_pulse will start with LOW
// SENSE line state: true = PLAY pressed (active), false = PLAY released (inactive)
// When true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
// When false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
bool TapePlayer::senseLineState = false;  // Start with PLAY released (inactive)
u64 TapePlayer::totalPlaybackTimeUs = 0;
u64 TapePlayer::cumulativePulseTimeUs = 0;
u64 TapePlayer::currentPulseOffsetUs = 0;
char TapePlayer::tapFilename[256] = {0};
volatile bool TapePlayer::editingPulses = false;
volatile bool TapePlayer::corePulseActive = false;

void TapePlayer::PausePlayback()
{
	editingPulses = true;
	Pi1551Barrier();
	Pi1551Wake();
	while (corePulseActive) Pi1551Barrier();
}
void TapePlayer::ResumePlayback()
{
	Pi1551Barrier();
	editingPulses = false;
	Pi1551Wake();
}
void TapePlayer::SetMotorAlwaysOn(bool value)
{
	if (recording) DisarmRecordIRQ();
	motorAlwaysOn = value;
	RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_MOTOR_GPIO);
	if (recording) ArmRecordIRQ();
	Tick10ms();
}
bool TapePlayer::Eject()
{
	if ((recording || unsaved) && !Stop()) return false;
	PausePlayback(); loaded = false; playing = false; atEnd = false; senseLineState = false;
	tapFilename[0] = 0; currentPulseIndex = 0; cumulativePulseTimeUs = 0; currentPulseOffsetUs = 0;
	totalPlaybackTimeUs = 0; FreePulseBuffer();
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO); readLineState = true;
	ResumePlayback();
	return true;
}
static void FinalizeRecordIndex(u64* timings, size_t& end, size_t& count, size_t capacity);
bool TapePlayer::Play()
{
	if (!loaded || recording) return false;
	playing = true; senseLineState = true;
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	Pi1551Wake();
	return true;
}
bool TapePlayer::Stop()
{
	if (!loaded) return false;
	if (unsaved && !recording) {
		unsaved = !SaveTap();
		return !unsaved;
	}
	if (recording)
	{
		DisarmRecordIRQ();
		PausePlayback();
		recording = false;
		if (recordMotor && currentPulseIndex < pulseCapacity) {
			u32 elapsed = Pi1551Clock() - recordEdgeTime;
			if (elapsed) { pulseTimings[currentPulseIndex++] = elapsed; unsaved = true; }
		}
		if (currentPulseIndex > pulseCount) pulseCount = currentPulseIndex;
		if (unsaved) FinalizeRecordIndex(pulseTimings, currentPulseIndex, pulseCount, pulseCapacity);
		// VICE consumes v2 half-waves in pairs. Close an odd final pair.
		if (unsaved && (pulseCount & 1) && pulseCount < pulseCapacity) pulseTimings[pulseCount++] = 1;
		totalPlaybackTimeUs = 0;
		for (size_t i = 0; i < pulseCount; ++i) totalPlaybackTimeUs += pulseTimings[i];
		cumulativePulseTimeUs = 0;
		for (size_t i = 0; i < currentPulseIndex; ++i) cumulativePulseTimeUs += pulseTimings[i];
		currentPulseOffsetUs = 0;
		atEnd = currentPulseIndex == pulseCount;
		bool saved = !unsaved || SaveTap();
		unsaved = !saved;
		ResumePlayback();
		playing = false; senseLineState = false;
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		readLineState = true;
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
		Pi1551Wake();
		return saved;
	}
	playing = false; senseLineState = false;
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	readLineState = true;
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
	Pi1551Wake();
	return true;
}
bool TapePlayer::Rewind()
{
	return SeekCounter(0);
}
static u64 TapeCounterAtTimeMs(u64 elapsedMs);

bool TapePlayer::SeekCounter(unsigned counter)
{
	if (!loaded || counter > 999) return false;
	if (!Stop()) return false;
	TapeUIState state; GetUIState(state);
	if (counter > state.maxTapeCounter) counter = state.maxTapeCounter;
	// Inverse of the VICE spool formula used by GetUIState().
	const double d = 1.27e-5, r = 1.07e-2, v = 4.76e-2, g = 0.525, pi = 3.1415926535;
	double turns = counter / g + r / d;
	u64 maxCounter = TapeCounterAtTimeMs(totalPlaybackTimeUs / 1000);
	u64 target = (counter != 0 && maxCounter <= 999 && counter >= maxCounter)
		? totalPlaybackTimeUs
		: (u64)(((turns * turns - (r / d) * (r / d)) * d * pi / v) * 1000000.0);
	PausePlayback();
	u64 elapsed = 0; size_t index = 0;
	while (index < pulseCount && elapsed + pulseTimings[index] <= target) elapsed += pulseTimings[index++];
	currentPulseIndex = index;
	currentPulseOffsetUs = index < pulseCount ? target - elapsed : 0;
	cumulativePulseTimeUs = target; atEnd = index == pulseCount;
	readLineState = (index & 1) == 0;
	if (readLineState) RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
	else RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);
	ResumePlayback(); return true;
}
bool TapePlayer::SeekOffset(u32 tapOffset)
{
	if (!loaded || recording || unsaved || tapOffset < 20 ||
	    (u64)tapOffset >= 20u + (u64)tapDataSize) return false;
	FIL fp;
	if (f_open(&fp, tapFilename, FA_READ) != FR_OK) return false;
	bool ok = f_lseek(&fp, 20) == FR_OK;
	u32 position = 20, pulseIndex = 0;
	unsigned extensionBytes = 0;
	u8 buffer[4096];
	while (ok && position < tapOffset) {
		UINT n = 0;
		u32 count = tapOffset - position;
		if (count > sizeof(buffer)) count = sizeof(buffer);
		ok = f_read(&fp, buffer, count, &n) == FR_OK && n == count;
		for (u32 i = 0; ok && i < count; ++i, ++position) {
			if (extensionBytes) { --extensionBytes; continue; }
			if (tapVersion != 0 && buffer[i] == 0) extensionBytes = 3;
			pulseIndex += tapVersion == 2 ? 1 : 2;
		}
	}
	if (f_close(&fp) != FR_OK) ok = false;
	if (!ok || extensionBytes || pulseIndex >= pulseCount || !Stop()) return false;
	u64 elapsed = 0;
	for (u32 i = 0; i < pulseIndex; ++i) elapsed += pulseTimings[i];
	PausePlayback();
	currentPulseIndex = pulseIndex;
	currentPulseOffsetUs = 0;
	cumulativePulseTimeUs = elapsed;
	atEnd = false;
	readLineState = (pulseIndex & 1) == 0;
	if (readLineState) RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
	else RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);
	ResumePlayback();
	return true;
}
static u32 tapeLoadGeneration = 0;
static u32 tapeIndexGeneration = 0;
struct RecordedIndexMark {
    size_t pulse;
    std::string name;
    u32 offset;
    RecordedIndexMark(size_t p, const std::string& n, u32 o = 0) : pulse(p), name(n), offset(o) {}
};
static std::vector<RecordedIndexMark> recordIndexMarks;
static std::string recordIndexPath, recordIndexComments;
static bool recordIndexExisted = false;
static size_t recordStartPulse = 0, recordDecodeStartPulse = 0;
static volatile bool recordSawMotor = false;
static volatile bool recordMotorOffPending = false;
static volatile u32 recordMotorOffAt = 0;
static bool recordIndexUpdatePending = false;

static bool ReadRecordIndex(const char* tap, u8 version)
{
    recordIndexMarks.clear(); recordIndexComments.clear(); recordIndexExisted = false;
    recordIndexPath = tap;
    size_t dot = recordIndexPath.find_last_of('.');
    if (dot == std::string::npos) return false;
    recordIndexPath.resize(dot);
    recordIndexPath += ".idx";
    FILINFO info;
    FRESULT stat = f_stat(recordIndexPath.c_str(), &info);
    if (stat == FR_NO_FILE) {
        std::string upper = recordIndexPath.substr(0, recordIndexPath.size() - 4) + ".IDX";
        stat = f_stat(upper.c_str(), &info);
        if (stat == FR_NO_FILE) return true;
        recordIndexPath = upper;
    }
    if (stat != FR_OK || info.fsize > 65536) return false;
    recordIndexExisted = true;
    FIL fp;
    if (f_open(&fp, recordIndexPath.c_str(), FA_READ) != FR_OK) return false;
    std::string text;
    char buffer[512]; UINT count = 0; bool ok = true;
    do {
        if (f_read(&fp, buffer, sizeof(buffer), &count) != FR_OK) { ok = false; break; }
        text.append(buffer, count);
    } while (count == sizeof(buffer));
    if (f_close(&fp) != FR_OK || !ok) return false;
    std::vector<oled::TapeIndexEntry> entries;
    if (!oled::ParseUltimateTapeIndex(text, entries)) return false;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        size_t p = text.find_first_not_of(" \t\r", at);
        if (p != std::string::npos && p < end && (text[p] == ';' || text[p] == '#' || text[p] == '\''))
            recordIndexComments.append(text, at, end - at).push_back('\n');
        at = end + 1;
    }
    // Map old file offsets to pulse indices before SaveTap re-encodes the image.
    std::sort(entries.begin(), entries.end(), [](const oled::TapeIndexEntry& a, const oled::TapeIndexEntry& b) { return a.offset < b.offset; });
    if (f_open(&fp, tap, FA_READ) != FR_OK) return false;
    if (f_lseek(&fp, 20) != FR_OK) { f_close(&fp); return false; }
    u32 position = 20; size_t pulse = 0, entry = 0;
    unsigned extension = 0;
    do {
        if (f_read(&fp, buffer, sizeof(buffer), &count) != FR_OK) { ok = false; break; }
        for (UINT i = 0; i < count; ++i, ++position) {
            if (extension) { --extension; continue; }
            while (entry < entries.size() && entries[entry].offset < position) ++entry;
            while (entry < entries.size() && entries[entry].offset == position) {
                recordIndexMarks.push_back(RecordedIndexMark(pulse, entries[entry].name));
                ++entry;
            }
            if (version && buffer[i] == 0) extension = 3;
            pulse += version == 2 ? 1 : 2;
        }
    } while (count == sizeof(buffer));
    if (f_close(&fp) != FR_OK) ok = false;
    return ok;
}

static int C16Pulse(const u64* timings, size_t at, size_t end)
{
    if (at + 1 >= end) return -1;
    u64 duration = timings[at] + timings[at + 1];
    if (duration >= 300 && duration < 700) return 0; // short
    if (duration >= 700 && duration < 1400) return 1; // long
    if (duration >= 1400 && duration < 2800) return 2; // word marker
    return -1;
}

static bool ReadC16Byte(const u64* timings, size_t& at, size_t end, u8& value)
{
    if (at + 40 > end || C16Pulse(timings, at, end) != 2 || C16Pulse(timings, at + 2, end) != 1) return false;
    at += 4; value = 0; unsigned parity = 1;
    for (unsigned bit = 0; bit < 9; ++bit, at += 4) {
        int first = C16Pulse(timings, at, end), second = C16Pulse(timings, at + 2, end);
        unsigned one;
        if (first == 0 && second == 1) one = 0;
        else if (first == 1 && second == 0) one = 1;
        else return false;
        if (bit < 8) { value |= one << bit; parity ^= one; }
        else if (one != parity) return false;
    }
    return true;
}

struct C16HeaderCandidate {
    size_t pulse;
    std::string name;
    bool repeat;
    C16HeaderCandidate(size_t p, const std::string& n, bool r) : pulse(p), name(n), repeat(r) {}
};
static bool ReadC16Header(const u64* timings, size_t at, size_t end, C16HeaderCandidate& result)
{
    size_t start = at;
    u8 value = 0;
    if (!ReadC16Byte(timings, at, end, value) || (value != 0x89 && value != 0x09)) return false;
    bool repeat = value == 0x09;
    for (unsigned i = 1; i < 9; ++i)
        if (!ReadC16Byte(timings, at, end, value) || value != (repeat ? 0x09u - i : 0x89u - i)) return false;
    if (!ReadC16Byte(timings, at, end, value) || (value != 1 && value != 3 && value != 4)) return false;
    u8 checksum = value, data[191];
    for (unsigned i = 0; i < sizeof(data); ++i) {
        if (!ReadC16Byte(timings, at, end, data[i])) return false;
        checksum ^= data[i];
    }
    if (!ReadC16Byte(timings, at, end, value) || value != checksum) return false;
    std::string name;
    for (unsigned i = 4; i < 20; ++i) {
        u8 c = petscii2ascii(data[i]);
        if (c < 32 || c > 126) c = '?';
        name.push_back((char)c);
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) return false;
    result = C16HeaderCandidate(start, name, repeat);
    return true;
}

static void DecodeRecordedHeaders(const u64* timings, size_t start, size_t end,
                                  std::vector<RecordedIndexMark>& marks)
{
    std::vector<C16HeaderCandidate> found;
    for (size_t phase = 0; phase < 2; ++phase) {
        size_t at = start + phase;
        while (at + 40 < end) {
            if (C16Pulse(timings, at, end) != 0) { at += 2; continue; }
            size_t leader = at;
            do { at += 2; } while (at + 2 < end && C16Pulse(timings, at, end) == 0);
            if ((at - leader) / 2 < 32) continue;
            size_t limit = std::min(at + 128, end);
            for (size_t candidate = at; candidate + 40 < limit; candidate += 2) {
                C16HeaderCandidate header(0, "", false);
                if (ReadC16Header(timings, candidate, end, header)) {
                    header.pulse = leader;
                    found.push_back(header);
                    break;
                }
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const C16HeaderCandidate& a, const C16HeaderCandidate& b) { return a.pulse < b.pulse; });
    for (size_t i = 0; i + 1 < found.size(); ++i) {
        if (found[i].repeat || !found[i + 1].repeat || found[i].name != found[i + 1].name) continue;
        if (marks.size() >= 512) break;
        marks.push_back(RecordedIndexMark(found[i].pulse, found[i].name));
        ++i;
    }
}

static void FinalizeRecordIndex(u64* timings, size_t& end, size_t& count, size_t capacity)
{
    std::vector<RecordedIndexMark> decoded;
    DecodeRecordedHeaders(timings, recordDecodeStartPulse, end, decoded);
    // A C16 v2 TAP is played as pairs of half-waves starting at pulse zero.
    // Recording can begin on either WRITE level. If a header's leader begins
    // on the other phase, its bytes still decode offline but the KERNAL sees
    // mispaired waves on playback. Add one short half-wave before the leader.
    bool hasOldSuffix = count > end;
    size_t inserted = 0;
    for (size_t i = 0; i < decoded.size(); ++i) {
        size_t at = decoded[i].pulse;
        if (!(at & 1) || count + (hasOldSuffix ? 2 : 1) > capacity) continue;
        memmove(timings + at + 1, timings + at, (count - at) * sizeof(*timings));
        timings[at] = 240;
        ++count; ++end; ++inserted;
        for (size_t j = i; j < decoded.size(); ++j)
            if (decoded[j].pulse >= at) ++decoded[j].pulse;
        for (size_t j = 0; j < recordIndexMarks.size(); ++j)
            if (recordIndexMarks[j].pulse >= at) ++recordIndexMarks[j].pulse;
    }
    // Preserve the phase of any existing tape after a middle overwrite.
    if (hasOldSuffix && (inserted & 1)) {
        memmove(timings + end + 1, timings + end, (count - end) * sizeof(*timings));
        timings[end] = 240;
        ++count; ++end;
        for (size_t j = 0; j < recordIndexMarks.size(); ++j)
            if (recordIndexMarks[j].pulse >= end - 1) ++recordIndexMarks[j].pulse;
    }
    std::vector<RecordedIndexMark> kept;
    for (size_t i = 0; i < recordIndexMarks.size(); ++i)
        if (recordIndexMarks[i].pulse < recordStartPulse || recordIndexMarks[i].pulse >= end)
            kept.push_back(recordIndexMarks[i]);
    kept.insert(kept.end(), decoded.begin(), decoded.end());
    std::sort(kept.begin(), kept.end(), [](const RecordedIndexMark& a, const RecordedIndexMark& b) {
        return a.pulse < b.pulse;
    });
    if (kept.size() > 512) kept.erase(kept.begin() + 512, kept.end());
    recordIndexMarks.swap(kept);
    recordIndexUpdatePending = true;
}

static bool WriteRecordIndex(const std::vector<RecordedIndexMark>& marks)
{
    if (marks.empty()) {
        if (recordIndexExisted && f_unlink(recordIndexPath.c_str()) != FR_OK) return false;
        if (recordIndexExisted) ++tapeIndexGeneration;
        recordIndexExisted = false;
        return true;
    }
    std::string text = recordIndexComments;
    char line[32];
    for (size_t i = 0; i < marks.size(); ++i) {
        snprintf(line, sizeof(line), "0x%08X ", marks[i].offset);
        text += line; text += marks[i].name; text += '\n';
    }
    if (text.size() > 65536) return false;
    std::string temporary = recordIndexPath + ".tmp";
    std::string backup = recordIndexPath + ".bak";
    FILINFO info;
    if (f_stat(temporary.c_str(), &info) != FR_NO_FILE ||
        (recordIndexExisted && f_stat(backup.c_str(), &info) != FR_NO_FILE)) return false;
    FIL fp;
    if (f_open(&fp, temporary.c_str(), FA_WRITE | FA_CREATE_NEW) != FR_OK) return false;
    UINT count = 0;
    bool ok = f_write(&fp, text.data(), text.size(), &count) == FR_OK && count == text.size();
    if (f_close(&fp) != FR_OK) ok = false;
    if (!ok) { f_unlink(temporary.c_str()); return false; }
    if (recordIndexExisted && f_rename(recordIndexPath.c_str(), backup.c_str()) != FR_OK) {
        f_unlink(temporary.c_str()); return false;
    }
    if (f_rename(temporary.c_str(), recordIndexPath.c_str()) != FR_OK) {
        if (recordIndexExisted) f_rename(backup.c_str(), recordIndexPath.c_str());
        f_unlink(temporary.c_str()); return false;
    }
    if (recordIndexExisted) f_unlink(backup.c_str());
    recordIndexExisted = true;
    ++tapeIndexGeneration;
    return true;
}

static u64 TapeCounterAtTimeMs(u64 elapsedMs)
{
	const double d = 1.27e-5, r = 1.07e-2, v = 4.76e-2, g = 0.525, pi = 3.1415926535;
	double t = elapsedMs / 1000.0;
	double term1 = (v * t) / (d * pi);
	double term2 = (r * r) / (d * d);
	double counter = g * (std::sqrt(term1 + term2) - r / d);
	return static_cast<u64>(counter + 0.5);
}

// TAP file header structure
struct TAPHeader
{
	char signature[12];  // "C16-TAPE-RAW" for Plus/4
	u8 version;
	u8 machine;
	u8 video;
	u8 reserved;
	u32 dataSize;  // Little-endian
};

bool TapePlayer::CreateTap(const char* path)
{
	if (!path || !path[0] || strlen(path) >= sizeof(tapFilename)) return false;
	if ((recording || unsaved) && !Stop()) return false;
	FIL fp;
	if (f_open(&fp, path, FA_CREATE_NEW | FA_WRITE) != FR_OK) return false;
	TAPHeader header = {};
	memcpy(header.signature, "C16-TAPE-RAW", 12);
	header.version = 2;
	header.machine = 2;
	header.video = 0;
	// VICE requires at least three data bytes for a mounted TAP. Its own
	// empty-image creator writes one four-byte zero-length pulse.
	header.dataSize = 4;
	UINT written = 0;
	bool ok = f_write(&fp, &header, sizeof(header), &written) == FR_OK && written == sizeof(header);
	u8 emptyPulse[4] = {};
	if (ok) ok = f_write(&fp, emptyPulse, sizeof(emptyPulse), &written) == FR_OK && written == sizeof(emptyPulse);
	ok = f_close(&fp) == FR_OK && ok;
	if (!ok) { f_unlink(path); return false; }
	FILINFO info = {};
	strncpy(info.fname, path, sizeof(info.fname) - 1);
	info.fsize = sizeof(header) + sizeof(emptyPulse);
	return LoadTap(&info);
}

bool TapePlayer::Record()
{
	if (!loaded || recording || unsaved || !tapFilename[0]) return false;
	FIL check;
	if (f_open(&check, tapFilename, FA_WRITE | FA_OPEN_EXISTING) != FR_OK) return false;
	if (f_close(&check) != FR_OK) return false;
	if (!ReadRecordIndex(tapFilename, tapVersion)) return false;
	recordIndexMarks.erase(std::remove_if(recordIndexMarks.begin(), recordIndexMarks.end(),
		[](const RecordedIndexMark& mark) { return mark.pulse >= pulseCount; }), recordIndexMarks.end());
	recordIndexUpdatePending = false;
	PausePlayback();
	if (currentPulseIndex >= TAPE_MAX_HALFWAVES) { ResumePlayback(); return false; }
	if (pulseCapacity < TAPE_MAX_HALFWAVES) {
		u64* p = (u64*)realloc(pulseTimings, (size_t)TAPE_MAX_HALFWAVES * sizeof(u64));
		if (!p) { ResumePlayback(); return false; }
		pulseTimings = p;
		pulseCapacity = TAPE_MAX_HALFWAVES;
	}
	recordStartPulse = currentPulseIndex;
	// At a seek into a pulse, retain the part before the recording position.
	if (currentPulseOffsetUs && currentPulseIndex < pulseCount) {
		pulseTimings[currentPulseIndex] = currentPulseOffsetUs;
		++currentPulseIndex;
		currentPulseOffsetUs = 0;
		unsaved = true;
	}
	recordMotor = false;
	recordEdgeTime = 0;
	recordDecodeStartPulse = currentPulseIndex;
	recordSawMotor = false;
	recordMotorOffPending = false;
	recordMotorOffAt = 0;
	recording = true;
	playing = true;
	senseLineState = true;
	atEnd = false;
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
	ArmRecordIRQ();
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	ResumePlayback();
	return true;
}

void TapePlayer::ArmRecordIRQ()
{
#if !defined(PI1551_NATIVE_TEST) || defined(PI1551_NATIVE_GPIO_IRQ_TEST)
	const u32 motor = 1u << TAPE_MOTOR_GPIO;
	const u32 write = 1u << TAPE_WRITE_GPIO;
	const u32 mask = motor | write;
	TapeIRQWrite(ARM_GPIO_GPREN0, TapeIRQRead(ARM_GPIO_GPREN0) & ~mask);
	TapeIRQWrite(ARM_GPIO_GPFEN0, TapeIRQRead(ARM_GPIO_GPFEN0) & ~mask);
	TapeIRQWrite(ARM_GPIO_GPEDS0, mask); // GPIO events are acknowledged by writing one.
	RecordSample(Pi1551Clock());
	InterruptSystemConnectIRQ(ARM_IRQ_GPIO0, RecordGPIOIRQ, this);
	if (!motorAlwaysOn) {
		TapeIRQWrite(ARM_GPIO_GPREN0, TapeIRQRead(ARM_GPIO_GPREN0) | motor);
		TapeIRQWrite(ARM_GPIO_GPFEN0, TapeIRQRead(ARM_GPIO_GPFEN0) | motor);
	}
	if (recordMotor) {
		TapeIRQWrite(ARM_GPIO_GPREN0, TapeIRQRead(ARM_GPIO_GPREN0) | write);
		TapeIRQWrite(ARM_GPIO_GPFEN0, TapeIRQRead(ARM_GPIO_GPFEN0) | write);
	}
#endif
}

void TapePlayer::DisarmRecordIRQ()
{
#if !defined(PI1551_NATIVE_TEST) || defined(PI1551_NATIVE_GPIO_IRQ_TEST)
	const u32 mask = (1u << TAPE_MOTOR_GPIO) | (1u << TAPE_WRITE_GPIO);
	TapeIRQWrite(ARM_GPIO_GPREN0, TapeIRQRead(ARM_GPIO_GPREN0) & ~mask);
	TapeIRQWrite(ARM_GPIO_GPFEN0, TapeIRQRead(ARM_GPIO_GPFEN0) & ~mask);
	InterruptSystemDisconnectIRQ(ARM_IRQ_GPIO0);
	TapeIRQWrite(ARM_GPIO_GPEDS0, mask);
#endif
}

void TapePlayer::RecordGPIOIRQ(void* param)
{
#if !defined(PI1551_NATIVE_TEST) || defined(PI1551_NATIVE_GPIO_IRQ_TEST)
	TapePlayer* tape = static_cast<TapePlayer*>(param);
	const u32 motor = 1u << TAPE_MOTOR_GPIO;
	const u32 write = 1u << TAPE_WRITE_GPIO;
	u32 events = TapeIRQRead(ARM_GPIO_GPEDS0) & (motor | write);
	if (!events) return;
	u32 now = Pi1551Clock();
	TapeIRQWrite(ARM_GPIO_GPEDS0, events);
	if (!recording) return;
	bool wasRunning = recordMotor;
	tape->RecordSample(now);
	if ((events & motor) && !motorAlwaysOn && wasRunning != recordMotor) {
		if (recordMotor) {
			TapeIRQWrite(ARM_GPIO_GPEDS0, write);
			TapeIRQWrite(ARM_GPIO_GPREN0, TapeIRQRead(ARM_GPIO_GPREN0) | write);
			TapeIRQWrite(ARM_GPIO_GPFEN0, TapeIRQRead(ARM_GPIO_GPFEN0) | write);
		} else {
			TapeIRQWrite(ARM_GPIO_GPREN0, TapeIRQRead(ARM_GPIO_GPREN0) & ~write);
			TapeIRQWrite(ARM_GPIO_GPFEN0, TapeIRQRead(ARM_GPIO_GPFEN0) & ~write);
			TapeIRQWrite(ARM_GPIO_GPEDS0, write);
		}
	}
#else
	(void)param;
#endif
}

void TapePlayer::RecordSample(u32 now)
{
	if (!recording) return;
	bool motor = motorAlwaysOn || RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_MOTOR_GPIO) == RPI_IO_LO;
	if (!motor) {
		if (recordMotor && currentPulseIndex < pulseCapacity) {
			u32 elapsed = now - recordEdgeTime;
			if (elapsed) {
				pulseTimings[currentPulseIndex++] = elapsed;
				cumulativePulseTimeUs += elapsed;
				if (currentPulseIndex > pulseCount) pulseCount = currentPulseIndex;
				unsaved = true;
			}
		}
		recordMotor = false;
		if (!motorAlwaysOn && recordSawMotor && currentPulseIndex >= recordDecodeStartPulse + 8 &&
		    !recordMotorOffPending) {
			recordMotorOffAt = now;
			recordMotorOffPending = true;
		}
		return;
	}
	recordMotorOffPending = false;
	bool write = RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_WRITE_GPIO) == RPI_IO_HI;
	if (!recordMotor) { recordSawMotor = true; recordMotor = true; recordWrite = write; recordEdgeTime = now; return; }
	if (write == recordWrite) return;
	u32 elapsed = now - recordEdgeTime;
	if (elapsed && currentPulseIndex < pulseCapacity) {
		pulseTimings[currentPulseIndex++] = elapsed;
		cumulativePulseTimeUs += elapsed;
		if (currentPulseIndex > pulseCount) pulseCount = currentPulseIndex;
		unsaved = true;
	}
	if (currentPulseIndex == pulseCapacity) atEnd = true;
	recordWrite = write;
	recordEdgeTime = now;
}

bool TapePlayer::SaveTap()
{
	FIL fp;
	if (f_open(&fp, tapFilename, FA_WRITE | FA_OPEN_EXISTING) != FR_OK) return false;
	TAPHeader header = {};
	memcpy(header.signature, "C16-TAPE-RAW", 12);
	header.version = 2; header.machine = 2; header.video = tapeVideo;
	UINT written = 0;
	bool ok = f_write(&fp, &header, sizeof(header), &written) == FR_OK && written == sizeof(header);
	u32 size = 0;
	u8 output[4096]; UINT used = 0;
	double hz = tapeVideo == 1 ? TAPE_CLOCK_NTSC_HZ : TAPE_CLOCK_PAL_HZ;
	size_t nextMark = 0;
	for (size_t i = 0; ok && i < pulseCount; ++i) {
		while (recordIndexUpdatePending && nextMark < recordIndexMarks.size() &&
		       recordIndexMarks[nextMark].pulse == i)
			recordIndexMarks[nextMark++].offset = size + 20;
		u64 cycles = (u64)(pulseTimings[i] * hz / 1000000.0 + 0.5);
		u64 units = (cycles + 4) / 8;
		u8 b[4]; UINT n = 1;
		if (pulseCount >= 3 && units >= 1 && units <= 255) b[0] = (u8)units;
		else {
			// VICE decodes extended v2 lengths as CPU cycles shifted by three.
			if (cycles > 0xffffff) cycles = 0xffffff;
			b[0] = 0; b[1] = cycles & 255; b[2] = (cycles >> 8) & 255; b[3] = (cycles >> 16) & 255; n = 4;
		}
		if (used + n > sizeof(output)) {
			ok = f_write(&fp, output, used, &written) == FR_OK && written == used;
			used = 0;
		}
		if (ok) { memcpy(output + used, b, n); used += n; }
		size += n;
	}
	if (ok && used) ok = f_write(&fp, output, used, &written) == FR_OK && written == used;
	if (ok && size == 0) {
		u8 emptyPulse[4] = {};
		ok = f_write(&fp, emptyPulse, sizeof(emptyPulse), &written) == FR_OK && written == sizeof(emptyPulse);
		size = sizeof(emptyPulse);
	}
	if (ok) {
		header.dataSize = size;
		ok = f_lseek(&fp, 0) == FR_OK && f_write(&fp, &header, sizeof(header), &written) == FR_OK && written == sizeof(header);
		if (ok) ok = f_lseek(&fp, size + sizeof(header)) == FR_OK && f_truncate(&fp) == FR_OK;
	}
	ok = f_close(&fp) == FR_OK && ok;
	if (ok) { tapVersion = 2; tapDataSize = size; }
	if (ok && recordIndexUpdatePending) {
		if (nextMark != recordIndexMarks.size() || !WriteRecordIndex(recordIndexMarks)) ok = false;
		else recordIndexUpdatePending = false;
	}
	return ok;
}

// Plus/4/C16 tape clock frequencies (Hz), as used by VICE TAP implementation (tap.c)
// CAUTION: C16 timers always run in slow mode (CLK/2), see VICE clklist[]:
// PAL: (1773447 + 1) / 2 ≈ 886,724 Hz
// NTSC: (1789772 + 1) / 2 ≈ 894,886 Hz

TapePlayer::TapePlayer()
{
	// tapeMotorAlwaysOn is configured once at boot (options are read before construction)
	motorAlwaysOn = options.TapeMotorAlwaysOn() != 0;
	motorActive = motorAlwaysOn;
}

TapePlayer::~TapePlayer()
{
    Eject();
}

void TapePlayer::InitializeGPIO()
{
	// Refresh configuration in case options were parsed after construction
	motorAlwaysOn = options.TapeMotorAlwaysOn() != 0;

	if (!motorAlwaysOn)
	{
		// GPIO6 (MOTOR) - input with pull-up
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_MOTOR_GPIO);
	}

	// GPIO19 (READ) - output, start high.
	// Hardware does NOT invert READ: GPIO high = CBM high.
	// Initial state matches cbmtapepi effective state on CBM side: first pulse will go LOW.
	RPI_SetGpioOutput((rpi_gpio_pin_t)TAPE_READ_GPIO);
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // GPIO high = output high on CBM
	readLineState = true;

	// GPIO26 (WRITE) - input with pull-up; recording uses GPIO edge IRQs.
	RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_WRITE_GPIO);
	
	// GPIO0 (SENSE) - output, start with PLAY released (inactive state)
	// SENSE logic: senseLineState=true (SENSE=1) = PLAY pressed (active)
	//   - Set GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
	//   - Set GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
	RPI_SetGpioOutput((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);  // GPIO LOW → output HIGH → PLAY released (inactive)
	senseLineState = false;  // PLAY released (inactive)
}

bool TapePlayer::LoadTap(const FILINFO* fileInfo)
{
	if ((recording || unsaved) && !Stop()) return false;
	PausePlayback();
	struct Resume { ~Resume() { TapePlayer::ResumePlayback(); } } resume;
	// Refresh configuration each load (option is set once at boot)
	motorAlwaysOn = options.TapeMotorAlwaysOn() != 0;

	// Close any existing tape
	loaded = false;
	playing = false; senseLineState = false;
	recording = false;
	unsaved = false;
	tapFilename[0] = 0;
	currentPulseIndex = 0;
	currentPulseOffsetUs = 0;
	atEnd = false;
	// Respect configured motor behaviour (always on vs GPIO-driven)
	Tick10ms();
	totalPlaybackTimeUs = 0;
	cumulativePulseTimeUs = 0;
	FreePulseBuffer();
	tapDataSize = 0;
	
		// TAPE_SENSE: PLAY button released (inactive state) when TAP is unloaded
		// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		// Wake core 2 so it can detect tape unloaded and go back to sleep
		Pi1551Wake();

	if (!fileInfo || !fileInfo->fname)
		return false;

	// Copy filename
	strncpy(tapFilename, fileInfo->fname, sizeof(tapFilename) - 1);
	tapFilename[sizeof(tapFilename) - 1] = '\0';

	// Open and parse TAP file
	FIL fp;
	FRESULT res = f_open(&fp, fileInfo->fname, FA_READ);
	if (res != FR_OK)
		return false;

	bool success = ParseTAP(fp, fileInfo->fsize);
	f_close(&fp);

	if (success)
	{
		loaded = true;
		currentPulseIndex = 0;
		currentPulseOffsetUs = 0;
		atEnd = false;
		cumulativePulseTimeUs = 0;
		tapeLoadGeneration++;
		readLineState = true;  // Start with HIGH (output) like cbmtapepi on CBM side, first pulse will toggle to LOW
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // GPIO high = output high
		// A loaded cassette starts in the stopped state unless autoplay is enabled.
		if (options.TapeAutoplay()) Play();
		else Stop();
		
		// Motor behaviour from options: when always on, start immediately; otherwise wait for GPIO poll
		Tick10ms();
		
		// Wake core 2 AFTER setting all state (loaded, motorActive) so it can start generating pulses
		Pi1551Wake();
		// Core 2 playback loop will see loaded/motorActive and start generating pulses.
	}

	return success;
}

void TapePlayer::FreePulseBuffer()
{
	if (pulseTimings)
	{
		free(pulseTimings);
		pulseTimings = nullptr;
	}
    pulseCount = 0;
	pulseCapacity = 0;
}

bool TapePlayer::ParseTAP(FIL& fp, u32 fileSize)
{
	if (fileSize < 20)
		return false;  // Too small to be a valid TAP

	TAPHeader header;
	UINT bytesRead;

	// Read header
	if (f_read(&fp, &header, sizeof(header), &bytesRead) != FR_OK || bytesRead != sizeof(header))
		return false;

	// Validate signature
	if (memcmp(header.signature, "C16-TAPE-RAW", 12) != 0)
		return false;

	// Validate version (support v0, v1, and v2 as per VICE TAP spec)
	if (header.version != 0 && header.version != 1 && header.version != 2)
		return false;
	
	// Validate machine (0x02 = Plus/4/C16)
	if (header.machine != 0x02)
		return false;
	
	// Determine Plus/4 tape clock based on video standard (0 = PAL, 1 = NTSC)
	// Used to compute:
	// - TAP units → microseconds: unit_to_us = 8,000,000 / clockSpeed
	// - CPU cycles → microseconds: cycles_to_us = 1,000,000 / clockSpeed
	double clockSpeed      = (header.video == 1) ? (double)TAPE_CLOCK_NTSC_HZ : (double)TAPE_CLOCK_PAL_HZ;
	double unitToUs        = 8000000.0 / clockSpeed;  // 8 cycles per TAP unit
	double cyclesToUs      = 1000000.0 / clockSpeed;  // 1 cycle

	// Read data size (little-endian)
	u32 dataSize = header.dataSize;
	if (dataSize == 0 || dataSize > (fileSize - 20))
		dataSize = fileSize - 20;  // Use remaining file size if header says 0
	tapeVideo = header.video;
	tapVersion = header.version;
	tapDataSize = dataSize;
	if (dataSize == 0) { FreePulseBuffer(); totalPlaybackTimeUs = 0; return true; }

	// Allocate buffer for reading data
	u8* dataBuffer = (u8*)malloc(dataSize);
	if (!dataBuffer)
		return false;

	if (f_read(&fp, dataBuffer, dataSize, &bytesRead) != FR_OK)
	{
		free(dataBuffer);
		return false;
	}

	// Parse data using VICE TAP semantics:
	// - v0: each byte is a complete cycle in TAP units (8 cycles per unit)
	// - v1:
	//     * normal bytes (>0): complete cycle in TAP units
	//     * 0x00: extended length, next 3 bytes are absolute cycle count
	// - v2:
	//     * each byte is a half-wave in TAP units
	//     * 0x00: extended length, next 3 bytes are CPU cycles (half-wave)
	// We always convert to half-waves in microseconds for CoreLoop() to use.
	u32 dataIndex = 0;
	u64 totalTimeUs = 0;
	size_t newPulseCount = 0;

    // Bound the output by the encoded input size instead of reserving the full
    // 10M-pulse ceiling for every TAP, including very small files.
    size_t pulsesPerByte = header.version == 2 ? 1 : 2;
    size_t newCapacity = dataSize > TAPE_MAX_HALFWAVES / pulsesPerByte
        ? TAPE_MAX_HALFWAVES
        : (size_t)dataSize * pulsesPerByte;
    u64* newTimings = (u64*)malloc(newCapacity * sizeof(u64));
    if (!newTimings)
    {
        free(dataBuffer);
        return false;
    }

	if (header.version == 0)
	{
		// Version 0: each byte represents a complete low-high cycle in TAP units.
		// If byte is 0x00, use 256 units (overflow condition).
		while (dataIndex < bytesRead)
		{
			u32 unitValue = dataBuffer[dataIndex++];
			if (unitValue == 0x00)
				unitValue = 256;  // Overflow condition in v0

			// Split full cycle into two half-waves in TAP units.
			u32 halfWaveUnits = unitValue / 2;
			if (halfWaveUnits == 0)
				halfWaveUnits = 1;

			u64 pulseUs1 = (u64)(halfWaveUnits * unitToUs + 0.5);
			if (pulseUs1 == 0)
				pulseUs1 = 1;

			u64 pulseUs2 = (u64)((unitValue - halfWaveUnits) * unitToUs + 0.5);
			if (pulseUs2 == 0)
				pulseUs2 = 1;

			if (newPulseCount + 2 > newCapacity)
			{
				free(newTimings);
				free(dataBuffer);
				return false;
			}

			newTimings[newPulseCount++] = pulseUs1;
			newTimings[newPulseCount++] = pulseUs2;
			totalTimeUs += pulseUs1 + pulseUs2;
		}
	}
	else if (header.version == 1)
	{
		// Version 1: each byte is a full cycle unless 0x00, which indicates an extended
		// length specified in *cycles* (not TAP units) in the following 3 bytes.
		while (dataIndex < bytesRead)
		{
			bool isExtended = false;
			u32 value = 0;

			if (dataBuffer[dataIndex] == 0x00)
			{
				// Extended length (3 bytes, little-endian, in CPU cycles)
				if (dataIndex + 3 >= bytesRead)
					break;
				value = (u32)dataBuffer[dataIndex + 1] |
				        ((u32)dataBuffer[dataIndex + 2] << 8) |
				        ((u32)dataBuffer[dataIndex + 3] << 16);
				dataIndex += 4;
				isExtended = true;
			}
			else
			{
				// Normal byte in TAP units (8 cycles per unit)
				value = dataBuffer[dataIndex++];
				isExtended = false;
			}

			if (isExtended)
			{
				// Value is in CPU cycles: convert directly to microseconds and split.
				u64 cycleUs = (u64)(value * cyclesToUs + 0.5);
				if (cycleUs == 0)
					cycleUs = 1;

				u64 halfCycleUs = cycleUs / 2;
				if (halfCycleUs == 0)
					halfCycleUs = 1;

				u64 pulseUs1 = halfCycleUs;
				u64 pulseUs2 = cycleUs - halfCycleUs;
				if (pulseUs2 == 0)
					pulseUs2 = 1;

				if (newPulseCount + 2 > newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}

				newTimings[newPulseCount++] = pulseUs1;
				newTimings[newPulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
			else
			{
				// Normal byte: full cycle in TAP units (8 cycles per unit).
				u32 unitValue = value;
				u32 halfWaveUnits = unitValue / 2;
				if (halfWaveUnits == 0)
					halfWaveUnits = 1;

				u64 pulseUs1 = (u64)(halfWaveUnits * unitToUs + 0.5);
				if (pulseUs1 == 0)
					pulseUs1 = 1;

				u64 pulseUs2 = (u64)((unitValue - halfWaveUnits) * unitToUs + 0.5);
				if (pulseUs2 == 0)
					pulseUs2 = 1;

				if (newPulseCount + 2 > newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}

				newTimings[newPulseCount++] = pulseUs1;
				newTimings[newPulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
		}
	}
	else // header.version == 2
	{
		// Version 2: each byte represents a *half-wave* in TAP units.
		// If byte is 0x00, next 3 bytes are CPU cycles for the half-wave.
		while (dataIndex < bytesRead)
		{
			u32 unitValue = 0;

			bool extended = dataBuffer[dataIndex] == 0x00;
			if (extended)
			{
				// Extended length (3 bytes, little-endian, in CPU cycles)
				if (dataIndex + 3 >= bytesRead)
					break;
				unitValue = (u32)dataBuffer[dataIndex + 1] |
				            ((u32)dataBuffer[dataIndex + 2] << 8) |
				            ((u32)dataBuffer[dataIndex + 3] << 16);
				dataIndex += 4;
			}
			else
			{
				unitValue = dataBuffer[dataIndex++];
			}

			u64 pulseUs = (u64)(unitValue * (extended ? cyclesToUs : unitToUs) + 0.5);
			if (pulseUs == 0)
				pulseUs = 1;

			if (newPulseCount + 1 > newCapacity)
			{
				free(newTimings);
				free(dataBuffer);
				return false;
			}

			newTimings[newPulseCount++] = pulseUs;
			totalTimeUs += pulseUs;
		}
	}

	free(dataBuffer);

	if (newPulseCount == 0)
	{
		free(newTimings);
		return false;
	}

	FreePulseBuffer();
	pulseTimings = newTimings;
    pulseCount = newPulseCount;
	pulseCapacity = newCapacity;
	totalPlaybackTimeUs = totalTimeUs;
	return true;
}


void TapePlayer::OnMotorChange(bool motorHigh)
{
	// motorActive is runtime-configured: always on or GPIO-driven
	motorActive = motorAlwaysOn ? true : motorHigh;

	// Motor state is independent from tape loading and PLAY.
	// Wake core 2 so it can react to the new motor state.
	Pi1551Wake();
	// CoreLoop() uses motorActive to gate playback.
}

void TapePlayer::GetUIState(TapeUIState& state) const
{
	state.isLoaded = loaded;
	state.isPlaying = playing;
	state.isRecording = recording;
	state.motorActive = motorActive;
	state.atEnd = atEnd;
	state.loadGeneration = tapeLoadGeneration;
	state.indexGeneration = tapeIndexGeneration;
	strncpy(state.filename, tapFilename, sizeof(state.filename) - 1);
	state.filename[sizeof(state.filename) - 1] = '\0';
	state.currentPulseIndex = static_cast<u32>(currentPulseIndex);  // Debug: current pulse index
	// Debug: current pulse duration (show next pulse duration, or 0 if at end or not loaded)
	// Note: currentPulseIndex points to the pulse that was just processed,
	// so we show the NEXT pulse duration (currentPulseIndex) for clarity
	if (loaded && currentPulseIndex < pulseCount && pulseTimings)
		state.currentPulseDurationUs = static_cast<u32>(pulseTimings[currentPulseIndex]);
	else
		state.currentPulseDurationUs = 0;

	// Calculate tape counter (3-digit, 000-999)
	// Uses VICE's physical model formula: c = g * (sqrt(v*t/(d*pi) + r^2/d^2) - r/d)
	// This matches the original Commodore Datasette hardware behavior and VICE emulator
	// Formula models tape spool geometry - counter advances non-linearly as tape unwinds
	// Derived from currentPulseIndex (sum of pulse timings played so far)
	if (loaded)
	{
		u64 currentTimeMs64 = cumulativePulseTimeUs / 1000;
		u64 totalTimeMs64 = totalPlaybackTimeUs / 1000;
		u32 maxU32 = std::numeric_limits<u32>::max();
		state.currentTimeMs = (currentTimeMs64 > maxU32) ? maxU32 : static_cast<u32>(currentTimeMs64);
		state.totalTimeMs = (totalTimeMs64 > maxU32) ? maxU32 : static_cast<u32>(totalTimeMs64);
		
		// Convert to integer and wrap at 1000 (3-digit display: 000-999)
		u64 counterValue = TapeCounterAtTimeMs(currentTimeMs64) % 1000;
		state.tapeCounter = static_cast<u32>(counterValue);
		u64 maxCounter = TapeCounterAtTimeMs(totalTimeMs64);
		state.maxTapeCounter = static_cast<u32>(maxCounter > 999 ? 999 : maxCounter);
		
		// Calculate percentage
		if (totalTimeMs64 > 0)
		{
			// Use 64-bit to avoid overflow for long tapes
			u64 percent = (currentTimeMs64 * 100) / totalTimeMs64;
			state.percentage = (percent > 100) ? 100 : (u8)percent;
		}
		else
		{
			state.percentage = 0;
		}
	}
	else
	{
		state.tapeCounter = 0;
		state.maxTapeCounter = 0;
		state.currentTimeMs = 0;
		state.totalTimeMs = 0;
		state.percentage = 0;
	}
}

void TapePlayer::Tick10ms()
{
	// This is called periodically from main loop
	// We could update counter here, but GetUIState() calculates it on-demand
	bool motorNow = motorAlwaysOn ? true
	                              : (RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_MOTOR_GPIO) == RPI_IO_LO);

	// When always-on, still call handler to keep SENSE asserted and wake core 2 after load/reset.
	if (motorAlwaysOn || motorNow != motorActive)
		OnMotorChange(motorNow);
	if (recording && !motorAlwaysOn && !motorNow && recordMotorOffPending &&
	    (u32)(Pi1551Clock() - recordMotorOffAt) > 500000u) Stop();
}

void TapePlayer::Reset()
{
	if (loaded) SeekCounter(0);
}

void TapePlayer::CoreLoop()
{
	// Core 2 generates READ pulses; GPIO edge IRQs on core 0 collect WRITE pulses.
	// Recording and idle playback wait for events instead of polling GPIO.
	// When idle (no tape loaded, motor inactive, or tape finished), core enters low-power
	// mode using WFE (Wait For Event) and is woken by SEV (Send Event) when state changes.

	// Ensure GPIOs are configured on this core.
	InitializeGPIO();

	while (1)
	{
		corePulseActive = false;
		Pi1551Barrier();
		if (editingPulses) { Pi1551Wait(); continue; }
		corePulseActive = true;
		Pi1551Barrier();
		if (editingPulses) continue;
		if (recording) {
			corePulseActive = false;
			Pi1551Wait();
			continue;
		}
		// Wait until we have a valid tape loaded
		if (!loaded || !playing || pulseTimings == nullptr || pulseCount == 0)
		{
			// Put core into low-power mode until tape is loaded
			// Core will be woken by SEV when LoadTap() is called
			corePulseActive = false;
			Pi1551Wait();
			continue;
		}

		// Motor must be active and not at end of tape
		if (!motorActive || atEnd)
		{
			// Put core into low-power mode until motor becomes active
			// Core will be woken by SEV when OnMotorChange() is called
			corePulseActive = false;
			Pi1551Wait();
			continue;
		}

		// End-of-tape handling: hold READ high and SENSE released
		if (currentPulseIndex >= pulseCount)
		{
			if (!atEnd)
			{
				atEnd = true;
				readLineState = true;  // HIGH output
				RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // GPIO high -> output high
				// PLAY stays pressed until the loaded TAP is explicitly ejected.
			}
			// Put core into low-power mode (tape finished)
			// Core will be woken by SEV if tape is reset or new tape loaded
			corePulseActive = false;
			Pi1551Wait();
			continue;
		}

		// Get current half-wave duration
		u64 pulseDurationUs = pulseTimings[currentPulseIndex];
		u64 pulseOffsetUs = currentPulseOffsetUs;
		currentPulseOffsetUs = 0;
		if (pulseOffsetUs >= pulseDurationUs)
			pulseOffsetUs = pulseDurationUs ? pulseDurationUs - 1 : 0;
		u64 intervalUs = pulseDurationUs - pulseOffsetUs;
		if (intervalUs == 0)
			intervalUs = 1;

		// Busy-wait using free-running system timer (1MHz)
		// Update cumulative time during long waits to keep counter accurate
		u32 start = Pi1551Clock();
		u32 lastUpdateUs = 0;
		const u32 updateIntervalUs = 10000;  // Update every 10ms during long waits
		
		while ((u32)(Pi1551Clock() - start) < (u32)intervalUs)
		{
			// If tape is unloaded or motor stopped during wait, abort this pulse
			if (editingPulses || !loaded || !playing || !motorActive || atEnd)
				break;
			
			// For long pulses, update cumulative time periodically to keep counter accurate
			u32 elapsedUs = (u32)(Pi1551Clock() - start);
			if (elapsedUs - lastUpdateUs >= updateIntervalUs)
			{
				cumulativePulseTimeUs += (elapsedUs - lastUpdateUs);
				lastUpdateUs = elapsedUs;
			}
		}

		if (editingPulses) continue;
		if (!loaded || !playing || !motorActive || atEnd)
		{
			// State changed while waiting; update time for elapsed portion
			u32 finalElapsedUs = (u32)(Pi1551Clock() - start);
			cumulativePulseTimeUs += (finalElapsedUs - lastUpdateUs);
			currentPulseOffsetUs = pulseOffsetUs + finalElapsedUs;
			// Re-evaluate outer loop conditions
			continue;
		}

		// Toggle READ line (no hardware inversion: GPIO high = CBM high, GPIO low = CBM low)
		readLineState = !readLineState;
		if (readLineState)
			RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // High output -> set GPIO high
		else
			RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Low output -> set GPIO low

		// Update timing/debug state
		// Use actual elapsed time instead of planned intervalUs to keep counter accurate
		// This ensures counter continues during long pauses/gaps in TAP
		u32 finalElapsedUs = (u32)(Pi1551Clock() - start);
		if (lastUpdateUs > 0)
		{
			// Add remaining time that wasn't updated in the loop
			cumulativePulseTimeUs += (finalElapsedUs - lastUpdateUs);
		}
		else
		{
			// Short pulse - add all elapsed time at once
			cumulativePulseTimeUs += finalElapsedUs;
		}
		currentPulseOffsetUs = 0;
		currentPulseIndex++;
	}
}

void TapePlayer::ToggleTapeRead()
{
	// Toggle READ line state (no hardware inversion: GPIO high = CBM high, GPIO low = CBM low)
	readLineState = !readLineState;
	if (readLineState)
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // High output -> set GPIO high
	else
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Low output -> set GPIO low
}

void TapePlayer::ToggleTapeSense()
{
	// Toggle SENSE line state (hardware inverts: GPIO high = low output/PLAY pressed, GPIO low = high output/PLAY released)
	senseLineState = !senseLineState;
	if (senseLineState)
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);  // Want PLAY pressed (low output) -> set GPIO high
	else
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);  // Want PLAY released (high output) -> set GPIO low
}

bool TapePlayer::GetTapeReadState() const
{
	// Return actual output state (not GPIO state)
	// Hardware inverts: GPIO low = output high, GPIO high = output low
	return readLineState;  // true = high output, false = low output
}

bool TapePlayer::GetTapeSenseState() const
{
	// Returns senseLineState: true = PLAY pressed (active), false = PLAY released (inactive)
	// When true: GPIO is HIGH, hardware inverts to output LOW, computer sees LOW = active
	// When false: GPIO is LOW, hardware inverts to output HIGH, computer sees HIGH = inactive
	return senseLineState;
}
