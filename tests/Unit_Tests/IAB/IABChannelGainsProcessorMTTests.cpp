// ChannelGainsProcessorMT: a channel with a zero gain is skipped (no ramp, product, lock or sum) and the gain state it
// leaves is the one the full path would leave.

#include <vector>

#include "gtest/gtest.h"
#include "renderer/IABRendererMT.h"

using namespace IABGAINSPROC;
using SMPTE::ImmersiveAudioBitstream::IABMutex;

namespace
{

const uint32_t kSamples = 250;
const uint32_t kChannels = 4;

struct Fixture
{
    std::vector<IABMutex> mutexes;
    std::vector<float> input;
    std::vector<std::vector<float>> output;
    float *outputPointers[kChannels];
    ChannelGainsProcessorMT processor;

    Fixture() : mutexes(kChannels), input(kSamples), output(kChannels, std::vector<float>(kSamples, 0.25f)), processor(mutexes)
    {
        for (uint32_t s = 0; s < kSamples; ++s)
        {
            input[s] = 0.001f * static_cast<float>(s + 1);
        }
        for (uint32_t c = 0; c < kChannels; ++c)
        {
            outputPointers[c] = output[c].data();
        }
    }
};

TEST(ChannelGainsProcessorMT, ZeroGainsWithoutSmoothing)
{
    Fixture f;
    EntityPastChannelGains state;
    std::vector<float> gains = {0.0f, 0.5f, 0.0f, 2.0f};
    // Output buffers are not initialised, so a skipped channel must leave its 0.25 untouched and the others add to it.
    ASSERT_EQ(kGainsProcNoError, f.processor.ApplyChannelGains(1, state, f.input.data(), kSamples, f.outputPointers,
                                                               kChannels, false, gains, false));
    for (uint32_t s = 0; s < kSamples; ++s)
    {
        EXPECT_EQ(0.25f, f.output[0][s]);
        EXPECT_EQ(0.25f + f.input[s] * 0.5f, f.output[1][s]);
        EXPECT_EQ(0.25f, f.output[2][s]);
        EXPECT_EQ(0.25f + f.input[s] * 2.0f, f.output[3][s]);
    }
    EXPECT_EQ(gains, state.channelGains_);
    EXPECT_TRUE(state.touched_);
    EXPECT_TRUE(state.gainsValid_);
}

TEST(ChannelGainsProcessorMT, SilentChannelsWithSmoothing)
{
    Fixture f;
    EntityPastChannelGains state;
    state.channelGains_ = {0.0f, 0.0f, 1.0f, 0.0f};
    state.gainsValid_ = true;
    state.touched_ = false;
    std::vector<float> target = {0.0f, 0.5f, 1.0f, 0.0f};
    ASSERT_EQ(kGainsProcNoError, f.processor.ApplyChannelGains(1, state, f.input.data(), kSamples, f.outputPointers,
                                                               kChannels, true, target, true));
    for (uint32_t s = 0; s < kSamples; ++s)
    {
        EXPECT_EQ(0.0f, f.output[0][s]); // silent before and after: nothing added, output initialised to 0
        EXPECT_EQ(f.input[s], f.output[2][s]); // unchanged gain 1
        EXPECT_EQ(0.0f, f.output[3][s]);
    }
    EXPECT_FLOAT_EQ(0.5f * f.input[kSamples - 1], f.output[1][kSamples - 1]); // the ramp reaches its target
    EXPECT_EQ(0.0f, state.channelGains_[0]);
    EXPECT_EQ(0.0f, state.channelGains_[3]);
    EXPECT_FLOAT_EQ(0.5f, state.channelGains_[1]);
    EXPECT_TRUE(state.touched_);
    EXPECT_TRUE(state.gainsValid_);
}

} // namespace
