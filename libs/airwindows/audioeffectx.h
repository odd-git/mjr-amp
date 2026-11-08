// Minimal stand-in for the VST2 SDK classes the Airwindows LinuxVST sources derive from,
// so their DSP compiles unchanged inside MJR Amp. Only what those sources touch is here.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef int32_t VstInt32;
typedef void* audioMasterCallback;
enum VstPlugCategory { kPlugCategEffect };
enum { kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 8, kVstMaxVendorStrLen = 64, kVstMaxProductStrLen = 64, kVstMaxEffectNameLen = 32 };

inline void vst_strncpy(char* dst, const char* src, size_t maxLen)
{
    std::strncpy(dst, src, maxLen);
    dst[maxLen] = 0;
}
inline void float2string(float value, char* text, VstInt32 maxLen) { std::snprintf(text, (size_t)maxLen + 1, "%.3f", value); }

class AudioEffect
{
public:
    AudioEffect(audioMasterCallback, VstInt32, VstInt32) {}
    virtual ~AudioEffect() = default;
    virtual void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) = 0;
    virtual void setParameter(VstInt32, float) {}
    virtual float getParameter(VstInt32) { return 0; }
    void setSampleRate(float sr) { sampleRate = sr; }
    float getSampleRate() { return sampleRate; }

protected:
    void setNumInputs(VstInt32) {}
    void setNumOutputs(VstInt32) {}
    void setUniqueID(VstInt32) {}
    void canProcessReplacing(bool = true) {}
    void canDoubleReplacing(bool = true) {}
    void programsAreChunks(bool = true) {}
    float sampleRate = 44100.0f;
};

class AudioEffectX : public AudioEffect
{
public:
    using AudioEffect::AudioEffect;
};
