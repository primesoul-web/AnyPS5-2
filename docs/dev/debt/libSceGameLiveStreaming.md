# libSceGameLiveStreaming technical debt

### Silent stubs

- [sceGameLiveStreamingGetProgramInfo](../../../core/libs/prx/libSceGameLiveStreaming/Export.cpp) (libSceGameLiveStreaming) returns 0 and stays parameterless, as in [shadPS4](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/game_live_streaming/gamelivestreaming.cpp). There is no live-stream program to describe. PPSA14632 imports it.

### Unknown function info

- [sceGameLiveStreamingGetCurrentStatus2](../../../core/libs/prx/libSceGameLiveStreaming/Export.cpp) (libSceGameLiveStreaming) - the status layout (user id, on-air flag, spectator, text and command message counts, broadcast resolution, 48 reserved bytes) is taken from shadPS4's `OrbisGameLiveStreamingStatus2`; the call reports that no stream is on air, with an invalid user id
