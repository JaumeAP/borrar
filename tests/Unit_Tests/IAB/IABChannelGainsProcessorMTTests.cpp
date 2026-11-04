// ChannelGainsProcessorMT: a channel with a zero gain is skipped (no ramp, product, lock or sum) and the gain state it
// leaves is the one the full path would leave.

#include <algorithm>
#include <cmath>
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

// The smoothing algorithm in double, written from its description: the first gain is one slope step past the
// current one, a ramp to the target over the ramp period (480 samples when the slope is capped, else the sub-block
// up to 4800), then the target. Returns the gain of the last sample.
double ReferenceSmoothedChannel(double current, double target, const std::vector<float> &input, uint32_t samples,
                                std::vector<double> &output)
{
    uint32_t const initRamp = samples < MAX_RAMP_SAMPLES ? samples : MAX_RAMP_SAMPLES;
    float const slopeF = (static_cast<float>(target) - static_cast<float>(current)) / initRamp;
    uint32_t ramp = initRamp;
    float slope = slopeF;
    if (slopeF > MAX_SLOPE) { slope = MAX_SLOPE; ramp = RAMP_SAMPLE_MAX_SLOPE; }
    else if (slopeF < -MAX_SLOPE) { slope = -MAX_SLOPE; ramp = RAMP_SAMPLE_MAX_SLOPE; }
    else if (slopeF == 0.0f) { ramp = 0; }

    double const first = static_cast<float>(current) + slope;
    double last = target;
    for (uint32_t s = 0; s < samples; ++s)
    {
        double gain = target;
        if (s < ramp)
        {
            gain = ramp >= 2 ? first + (target - first) * s / (ramp - 1) : first;
        }
        output[s] += input[s] * gain;
        last = gain;
    }
    return last;
}

// Ramps up, down, capped (longer than the sub-block), shorter than the sub-block, from and to zero, and a channel
// whose gain does not change while another one does, over three sub-blocks in a row: the output and the gain state
// stay within float rounding of the reference.
TEST(ChannelGainsProcessorMT, SmoothingMatchesTheReference)
{
    std::vector<float> const starts = {0.0f, 0.9f, 0.2f, 0.7f, 0.3f, 1.0f};
    std::vector<float> const targets = {0.8f, 0.1f, 0.25f, 0.0f, 0.3f, 0.95f};
    uint32_t const channels = static_cast<uint32_t>(starts.size());

    for (uint32_t samples : {250u, 2000u})
    {
        std::vector<IABMutex> mutexes(channels);
        ChannelGainsProcessorMT processor(mutexes);
        std::vector<float> input(samples);
        std::vector<std::vector<float>> output(channels, std::vector<float>(samples, 0.0f));
        std::vector<std::vector<double>> reference(channels, std::vector<double>(samples, 0.0));
        std::vector<float *> pointers(channels);
        std::vector<double> referenceGains(starts.begin(), starts.end());

        EntityPastChannelGains state;
        state.channelGains_ = starts;
        state.gainsValid_ = true;

        for (int block = 0; block < 3; ++block)
        {
            for (uint32_t s = 0; s < samples; ++s)
            {
                input[s] = std::sin(0.01f * static_cast<float>(s + block * samples)) * 0.8f;
            }
            for (uint32_t c = 0; c < channels; ++c)
            {
                std::fill(output[c].begin(), output[c].end(), 0.0f);
                std::fill(reference[c].begin(), reference[c].end(), 0.0);
                pointers[c] = output[c].data();
                referenceGains[c] = ReferenceSmoothedChannel(referenceGains[c], targets[c], input, samples, reference[c]);
            }

            ASSERT_EQ(kGainsProcNoError, processor.ApplyChannelGains(1, state, input.data(), samples, pointers.data(),
                                                                     channels, false, targets, true));

            for (uint32_t c = 0; c < channels; ++c)
            {
                for (uint32_t s = 0; s < samples; ++s)
                {
                    ASSERT_NEAR(reference[c][s], output[c][s], 2e-6) << "samples " << samples << " block " << block
                                                                    << " channel " << c << " sample " << s;
                }
                EXPECT_NEAR(referenceGains[c], state.channelGains_[c], 2e-6) << "channel " << c << " block " << block;
            }
        }
    }
}

} // namespace
