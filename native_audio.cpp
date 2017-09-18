// Copyright 2017, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//
#define LOG_NDEBUG 0
#define LOG_TAG "native_audio"
#include <utils/Log.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utils/threads.h>
#include <cutils/properties.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
#include <binder/ProcessState.h>
#include <fcntl.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

int timeout_input(int *input_value, int timeout_sec)
{
    char buffer[80] = {0};
    fd_set readfds;
    int ready_for_reading = 0;
    int read_bytes = 0;
    struct timeval timeout;
    int fd_stdin;

    fd_stdin = fileno(stdin);

    FD_ZERO(&readfds);
    FD_SET(fd_stdin, &readfds);

    fflush(stdin);

    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;
    ready_for_reading = select(fd_stdin + 1, &readfds, NULL, NULL, &timeout);
    if (ready_for_reading == -1) {
        /* Some error has occured in input */
        return -1;
    }

    if (ready_for_reading) {
        read_bytes = read(fd_stdin, buffer, 20);
        if (buffer[read_bytes-1]=='\n') {
            --read_bytes;
            buffer[read_bytes]='\0';
        }

        if (read_bytes == 0) {
            // just hit enter
            return 1;
        } else {
            //printf("Read, %d bytes from input : %s \n", read_bytes, buffer);
            *input_value = atoi(buffer);
        }
    } else {
        // timeout
        return 100;
    }

    return 0;
}

const char *const state_str[] = {
    "STATE_INIT",
    "STATE_PLAY",
    "STATE_STOP",
    "STATE_PAUSE",
    "STATE_FILE_EMPTY",
    "RESERVED1",
    "RESERVED2",
    "RESERVED3",
};

using namespace android;

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

class NativeAudio : public Thread {
public:
    NativeAudio(char *path_name);
    virtual ~NativeAudio();
    static void audio_callback(int event, void* user, void *info);

    int track_callback_setup(void);
    int check_wav_file(void);

    virtual status_t readyToRun();
    virtual bool threadLoop();

    class NA_VisualStatus : public Thread {
    public:
        NA_VisualStatus(const sp<AudioTrack>& track):played_track(track)
        {
        }
        virtual ~NA_VisualStatus(){};
        virtual bool threadLoop();
        const sp<AudioTrack> played_track;
    };

    void get_output_test();

    enum State {
        STATE_INIT,
        STATE_PLAY,
        STATE_STOP,
        STATE_PAUSE,
        STATE_FILE_EMPTY,
        STATE_LAST,
    } mState;

    sp<AudioTrack> play_track;

    sp<NA_VisualStatus> vst;

    float track_volume;
    float master_volume;

    struct wav_header header;
    char *wavfile_path;
    FILE *wavfile;
};

NativeAudio::NativeAudio(char *path_name)
{
    wavfile = NULL;
    wavfile_path = path_name;
    track_volume = 1.0f;
}

NativeAudio::~NativeAudio()
{
    if (wavfile)
        fclose(wavfile);
}

status_t NativeAudio::readyToRun()
{
    if (check_wav_file() < 0)
        return NAME_NOT_FOUND;

    if (track_callback_setup() < 0)
        return INVALID_OPERATION;

    vst = new NA_VisualStatus(play_track);
    if (vst == 0) {
        return NO_MEMORY;
    }

    mState = STATE_INIT;

    return NO_ERROR;
}

bool NativeAudio::NA_VisualStatus::threadLoop()
{
    AudioTimestamp ts;
    while (!exitPending()) {
        status_t status = played_track->getTimestamp(ts);
        if (OK == status) {
            //ALOGI("status: m(%d) plated:%ld.%ld\n", ts.mPosition, ts.mTime.tv_sec, ts.mTime.tv_nsec);
        } else {
            //ALOGI("status: error %d\n", status);
        }
        usleep(100 * 1000);
    }
    return false;
}

void NativeAudio::get_output_test()
{
    audio_io_handle_t output;
    output = AudioSystem::getOutput(AUDIO_STREAM_VOICE_CALL);
    ALOGD("getOutput:[%d][AUDIO_STREAM_VOICE_CALL] handle %d \n", AUDIO_STREAM_VOICE_CALL, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_SYSTEM);
    ALOGD("getOutput:[%d][AUDIO_STREAM_SYSTEM] handle %d \n", AUDIO_STREAM_SYSTEM, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_RING);
    ALOGD("getOutput:[%d][AUDIO_STREAM_RING] handle %d \n", AUDIO_STREAM_RING, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_MUSIC);
    ALOGD("getOutput:[%d][AUDIO_STREAM_MUSIC] handle %d \n", AUDIO_STREAM_MUSIC, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_ALARM);
    ALOGD("getOutput:[%d][AUDIO_STREAM_ALARM] handle %d \n", AUDIO_STREAM_ALARM, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_NOTIFICATION);
    ALOGD("getOutput:[%d][AUDIO_STREAM_NOTIFICATION] handle %d \n", AUDIO_STREAM_NOTIFICATION, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_BLUETOOTH_SCO);
    ALOGD("getOutput:[%d][AUDIO_STREAM_BLUETOOTH_SCO] handle %d \n", AUDIO_STREAM_BLUETOOTH_SCO, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_ENFORCED_AUDIBLE);
    ALOGD("getOutput:[%d][AUDIO_STREAM_ENFORCED_AUDIBLE] handle %d \n", AUDIO_STREAM_ENFORCED_AUDIBLE, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_DTMF);
    ALOGD("getOutput:[%d][AUDIO_STREAM_DTMF] handle %d \n", AUDIO_STREAM_DTMF, output);
    output = AudioSystem::getOutput(AUDIO_STREAM_TTS);
    ALOGD("getOutput:[%d][AUDIO_STREAM_TTS] handle %d \n", AUDIO_STREAM_TTS, output);
}

bool NativeAudio::threadLoop()
{
    int menu;
    int ret;
    unsigned long played_bytes;
    int frame_size = (header.num_channels * (header.bits_per_sample / 8));

    unsigned int play_time;
    unsigned int total_time;

    total_time = (header.data_sz / frame_size) / header.sample_rate;
    while (!exitPending()) {
        menu = 0;
        ret = 0;
        system("clear");
        AudioSystem::getMasterVolume(&master_volume);
        played_bytes = ftell(wavfile);
        played_bytes -= sizeof(struct wav_header);
        play_time = (played_bytes / frame_size) / header.sample_rate;

        fprintf(stdout, "========================================================\n");
        fprintf(stdout, "  console wave file player on android   \n");
        fprintf(stdout, "   - wav file [%s]\n", wavfile_path);
        fprintf(stdout, "   - current status [%s]\n", state_str[mState]);
        fprintf(stdout, "   - current play bytes %lu/%u\n", played_bytes, header.data_sz);
        fprintf(stdout, "   - current play time %u/%u\n", play_time, total_time);
        fprintf(stdout, "   - current track track_volume [%f]\n", track_volume);
        fprintf(stdout, "   - current master track_volume [%f]\n", master_volume);
        fprintf(stdout, "========================================================\n");
        fprintf(stdout, "   1. Play\n");
        fprintf(stdout, "   2. Pause\n");
        fprintf(stdout, "   3. Stop\n");
        fprintf(stdout, "   4. track_volume up\n");
        fprintf(stdout, "   5. track_volume down\n");
        fprintf(stdout, "   6. Master track_volume up\n");
        fprintf(stdout, "   7. Master track_volume down\n");
        fprintf(stdout, "   10. RewindFirst\n");
        fprintf(stdout, "   100. Exit\n");
        fprintf(stdout, "   300. getoutput test\n");
        fprintf(stdout, ">> \n");
        ret = timeout_input(&menu, 1);
        if (ret != 0)
            menu = 0;

        switch(menu) {
            case 1:
                play_track->start();
                vst->run();
                mState = STATE_PLAY;
                break;

            case 2:
                play_track->pause();
                vst->requestExitAndWait();
                mState = STATE_PAUSE;
                break;

            case 3:
                play_track->stop();
                vst->requestExitAndWait();
                mState = STATE_STOP;
                break;

            case 4:
                track_volume = track_volume + 0.1f;
                if (track_volume > 1.0f)
                    track_volume = 1.0f;
                play_track->setVolume(track_volume);
                break;

            case 5:
                track_volume = track_volume - 0.1f;
                if (track_volume < 0)
                    track_volume = 0.0f;
                play_track->setVolume(track_volume);
                break;

            case 6:
                master_volume = master_volume + 0.1f;
                if (master_volume > 1.0f)
                    master_volume = 1.0f;
                AudioSystem::setMasterVolume(master_volume);
                break;

            case 7:
                master_volume = master_volume - 0.1f;
                if (master_volume < 0)
                    master_volume = 0.0f;
                AudioSystem::setMasterVolume(master_volume);
                break;

            case 10:
                if (mState == STATE_PLAY) {
                    play_track->flush();
                    play_track->stop();
                    vst->requestExitAndWait();
                    fseek(wavfile, sizeof(struct wav_header), SEEK_SET);
                    play_track->start();
                } else if (mState == STATE_STOP || mState == STATE_PAUSE) {
                    play_track->flush();
                    fseek(wavfile, sizeof(struct wav_header), SEEK_SET);
                }
                break;
            case 100:
                play_track->stop();
                vst->requestExitAndWait();
                mState = STATE_STOP;
                return false;

            case 300:
                get_output_test();
                break;
            case 0:
            default:
                if (mState == STATE_FILE_EMPTY) {
                    play_track->stop();
                    play_track->flush();
                    vst->requestExitAndWait();
                    usleep(100 * 1000);
                    fseek(wavfile, sizeof(struct wav_header), SEEK_SET);
                    mState = STATE_STOP;
                }
                break;
        };
    }

    return false;
}

void NativeAudio::audio_callback(int event, void* user, void *info)
{
    if (event != AudioTrack::EVENT_MORE_DATA) return;

    NativeAudio *nat = static_cast<NativeAudio *>(user);
    AudioTrack::Buffer *buffer = static_cast<AudioTrack::Buffer *>(info);
    char *buf = (char *)buffer->i8;

    if (buffer->size == 0) return; /* request size */

    buffer->size = fread(buf, 1, buffer->size, nat->wavfile);

    if (buffer->size == 0) {
        nat->mState = STATE_FILE_EMPTY;
    }
}

int NativeAudio::track_callback_setup()
{
    audio_format_t aft;
    audio_channel_mask_t acm;
    long rate = header.sample_rate;
    long playtime = header.data_sz / header.byte_rate;
    size_t minframecnt;
    status_t st;

    switch(header.bits_per_sample) {
        case 8:
            aft = AUDIO_FORMAT_PCM_8_BIT;
            break;
        case 16:
            aft = AUDIO_FORMAT_PCM_16_BIT;
            break;
        case 32:
            aft = AUDIO_FORMAT_PCM_32_BIT;
            break;
        default:
            return -1;
    };

    switch(header.num_channels) {
        case 1:
            acm = AUDIO_CHANNEL_OUT_MONO;
            break;
        case 2:
            acm = AUDIO_CHANNEL_OUT_STEREO;
            break;
        case 4:
            acm = AUDIO_CHANNEL_OUT_QUAD;
            break;
        default:
            return -1;
    };

    st = AudioTrack::getMinFrameCount(&minframecnt, AUDIO_STREAM_MUSIC, rate);
    if (st != NO_ERROR) {
        ALOGE("Failed the getMinFrameCount %d", st);
        return -1;
    }

    ALOGD("MUSIC Stream Min Frame Count %u", minframecnt);

    play_track = new AudioTrack();
    play_track->set(AUDIO_STREAM_MUSIC,  //TODO parameterize this
            rate,
            aft,
            audio_channel_out_mask_from_count(header.num_channels),
            minframecnt,
            AUDIO_OUTPUT_FLAG_PRIMARY,
            this->audio_callback,
            this, // user
            0,    // notificationFrames
            0,    // sharedBuffer
            false,
            0,    // sessionId
            AudioTrack::TRANSFER_CALLBACK);

    status_t status = play_track->initCheck();
    if(status != NO_ERROR) {
        play_track.clear();
        ALOGD("Failed for initCheck()");
        return -1;
    }

    play_track->setVolume(track_volume);

    return 0;
}

int NativeAudio::check_wav_file(void)
{
    if (!wavfile_path)
        return -1;

    wavfile = fopen(wavfile_path, "rb");
     if (!wavfile) {
        ALOGE("Unable to open file '%s'\n", wavfile_path);
        return -2;
    }

    fread(&header, sizeof(struct wav_header), 1, wavfile);

    if ((header.riff_id != ID_RIFF) ||
        (header.riff_fmt != ID_WAVE) ||
        (header.fmt_id != ID_FMT) ||
        (header.audio_format != FORMAT_PCM) ||
        (header.fmt_sz != 16)) {
        ALOGE("Error: '%s' is not a PCM riff/wave file\n", wavfile_path);
        fclose(wavfile);
        return -3;
    }

    ALOGI("Info: riff_id         0x%x\n", header.riff_id);
    ALOGI("Info: riff_sz         %d\n", header.riff_sz);
    ALOGI("Info: riff_fmt        0x%x\n", header.riff_fmt);
    ALOGI("Info: fmt_id          0x%x\n", header.fmt_id);
    ALOGI("Info: fmt_sz          %d\n", header.fmt_sz);
    ALOGI("Info: audio_format    0x%x\n", header.audio_format);
    ALOGI("Info: num_channels    %d\n", header.num_channels);
    ALOGI("Info: sample_rate     %d\n", header.sample_rate);
    ALOGI("Info: byte_rate       %d\n", header.byte_rate);
    ALOGI("Info: block_align     %d\n", header.block_align);
    ALOGI("Info: bits_per_sample %d\n", header.bits_per_sample);
    ALOGI("Info: data_id         0x%x\n", header.data_id);
    ALOGI("Info: data_sz         %d\n", header.data_sz);

    /*
    Info: riff_id  0x46464952
    Info: riff_sz  36822756
    Info: riff_fmt 0x45564157
    Info: fmt_id   0x20746d66
    Info: fmt_sz   16
    Info: audio_format 0x1
    Info: num_channels 2
    Info: sample_rate 44100
    Info: byte_rate 176400
    Info: block_align 4
    Info: bits_per_sample 16
    Info: data_id 0x61746164
    Info: data_sz 36822528
    */

    return 0;
}

int main(int argc, char *argv[])
{
    ProcessState::self()->startThreadPool();
    sp<NativeAudio> na;

    if (argc != 2) {
        ALOGE("Missing the wav file\n");
    }

    na = new NativeAudio(argv[1]);
    na->run();
    na->join();
    return 0;
}

