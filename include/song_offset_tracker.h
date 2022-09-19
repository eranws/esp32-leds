#ifndef __SONG_OFFSET_TRACKER_H__
#define __SONG_OFFSET_TRACKER_H__

#include <TimeSync.hpp>

class SongOffsetTracker
{
public:
    void setup();
    void loop(bool *isClockChanged, bool *clockFirstValid);

public:
    bool HandleCurrentSongMessage(char *data);
    const String &GetCurrentFile() const { return fileName; }
    bool IsSongPlaying() { return isSongPlaying; }

public:
    int32_t GetSongStartTime();

private:
    TimeSync::TimeSyncClient timesync;

private:
    bool isSongPlaying = false;
    int64_t songStartTimeEpoch; // valid only if isSongPlaying is true
    String fileNameFromPlayer;  // // valid only if isSongPlaying is true
    String fileName;            // valid only if isSongPlaying is true
};

#endif //__SONG_OFFSET_TRACKER_H__
