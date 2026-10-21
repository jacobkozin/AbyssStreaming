#include "core/AudioBackend.hpp"

namespace abyss {

const char *audioChannelLabel(AudioChannel channel)
{
    switch (channel) {
    case AudioChannel::Desktop: return "Desktop Audio";
    case AudioChannel::Microphone: return "Mic/Aux";
    }
    return "Audio";
}

} // namespace abyss
