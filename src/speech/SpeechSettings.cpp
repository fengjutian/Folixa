#include "speech/SpeechSettings.h"

namespace aipdf::speech {

SpeechSettings normalizeSpeechSettings(const SpeechSettings& settings) {
    SpeechSettings normalized = settings;
    if (normalized.rate < kSpeechRateMin) {
        normalized.rate = kSpeechRateMin;
    } else if (normalized.rate > kSpeechRateMax) {
        normalized.rate = kSpeechRateMax;
    }
    if (normalized.volume < 0) {
        normalized.volume = 0;
    } else if (normalized.volume > 100) {
        normalized.volume = 100;
    }
    normalized.voice = normalized.voice.trimmed();
    return normalized;
}

}  // namespace aipdf::speech
