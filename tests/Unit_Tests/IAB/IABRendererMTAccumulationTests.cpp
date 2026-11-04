// IABRendererMT with several worker threads gives the output of one worker thread: the threads add their objects and
// beds into the outputs in another order, so the samples agree within float rounding, not bit for bit. A characterisation
// test for the per-thread accumulation of the outputs.

#include <cmath>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "common/IABElements.h"
#include "IABRendererAPI.h"
#include "renderutils/IRendererConfiguration.h"
#include "testcfg.h"

using namespace SMPTE::ImmersiveAudioBitstream;

namespace
{

struct ObjectSpec
{
    float x, y, z;
    float gain;
    float spread;      // 1D spread, 0 for none
    bool snap;
    bool zones;
    bool decorrelate;
};

// When set, the decorrelated object has gain 0 (DecorrelatedObjectIsHeard).
bool gMuteDecorrelated = false;

// The objects of the test: distinct positions, one with spread, one with snap, one with zone gains, one decorrelated.
std::vector<ObjectSpec> Objects(uint32_t iFrame)
{
    std::vector<ObjectSpec> objects;
    for (uint32_t j = 0; j < 16; ++j)
    {
        float const t = 0.05f * static_cast<float>(iFrame);
        ObjectSpec o = {std::fmod(0.07f * j + t, 1.0f), std::fmod(0.13f * j + 0.5f * t, 1.0f), (j % 3) * 0.5f,
                        0.5f + 0.03f * j, 0.0f, false, false, false};
        objects.push_back(o);
    }
    objects[1].spread = 0.3f;
    objects[2].snap = true;
    objects[3].zones = true;
    objects[4].decorrelate = true;
    if (gMuteDecorrelated)
    {
        objects[4].gain = 0.0f;
    }
    return objects;
}

// The samples of channel iChannel of frame iFrame: a distinct tone per channel.
float Sample(uint32_t iFrame, uint32_t iChannel, uint32_t iSample, uint32_t iFrameSamples)
{
    double const n = double(iFrame) * iFrameSamples + iSample;
    return static_cast<float>(0.1 * std::sin(n * (0.001 + 0.0007 * iChannel)));
}

void FillSubBlock(ObjectSpec const &iSpec, IABObjectSubBlock &oSub)
{
    IABGain gain;
    gain.setIABGain(iSpec.gain);
    CartesianPosInUnitCube point;
    point.setIABObjectPosition(iSpec.x, iSpec.y, iSpec.z);
    IABObjectSnap snap = IABObjectSnap();
    snap.objectSnapPresent_ = iSpec.snap ? 1 : 0;
    snap.objectSnapTolerance_ = iSpec.snap ? 400 : 0;
    IABObjectZoneGain9 zones = IABObjectZoneGain9();
    zones.objectZoneControl_ = iSpec.zones ? 1 : 0;
    for (int z = 0; z < 9 && iSpec.zones; ++z)
    {
        zones.zoneGains_[z].setIABZoneGain(z % 2 == 0 ? 1.0f : 0.25f);
    }
    IABObjectSpread spread;
    spread.setIABObjectSpread(kIABSpreadMode_HighResolution_1D, iSpec.spread, iSpec.spread, iSpec.spread);
    IABDecorCoeff decor = IABDecorCoeff();
    decor.decorCoefPrefix_ = iSpec.decorrelate ? kIABDecorCoeffPrefix_MaxDecor : kIABDecorCoeffPrefix_NoDecor;
    oSub.SetPanInfoExists(1);
    oSub.SetObjectGain(gain);
    oSub.SetObjectPositionFromUnitCube(point);
    oSub.SetObjectSnap(snap);
    oSub.SetObjectZoneGains9(zones);
    oSub.SetObjectSpread(spread);
    oSub.SetDecorCoef(decor);
}

// One frame: iBeds bed channels in one bed (the 10 of a 7.1.2 bed when iBeds), iObjects objects; float audio elements.
IABFrameInterface *MakeFrame(uint32_t iFrame, bool iBeds, bool iObjects, IABSampleRateType iRate, uint32_t iFrameSamples)
{
    static IABChannelIDType const kBed[10] = {kIABChannelID_Left, kIABChannelID_Center, kIABChannelID_Right,
                                              kIABChannelID_LeftSideSurround, kIABChannelID_LeftRearSurround,
                                              kIABChannelID_RightRearSurround, kIABChannelID_RightSideSurround,
                                              kIABChannelID_LFE, kIABChannelID_LeftTopSurround,
                                              kIABChannelID_RightTopSurround};
    std::vector<IABElement *> audio, elements;
    std::vector<float> samples(iFrameSamples);
    IABAudioDataIDType nextId = 1;
    auto data = [&](uint32_t iChannel) {
        for (uint32_t s = 0; s < iFrameSamples; ++s)
        {
            samples[s] = Sample(iFrame, iChannel, s, iFrameSamples);
        }
        IABAudioDataFloat *element = new IABAudioDataFloat(iFrameSamples);
        element->SetAudioDataID(nextId);
        element->SetSamples(samples.data(), iFrameSamples);
        audio.push_back(element);
        return nextId++;
    };
    uint32_t rendered = 0;
    if (iBeds)
    {
        IABBedDefinition *bed = new IABBedDefinition(kIABFrameRate_24FPS);
        bed->SetMetadataID(1);
        bed->SetConditionalBed(0);
        std::vector<IABChannel *> channels;
        for (uint32_t c = 0; c < 10; ++c)
        {
            IABChannel *channel = new IABChannel();
            IABGain unity;
            unity.setIABGain(1.0f);
            channel->SetChannelID(kBed[c]);
            channel->SetAudioDataID(data(c));
            channel->SetChannelGain(unity);
            channel->SetDecorInfoExists(0);
            channels.push_back(channel);
        }
        bed->SetBedChannels(channels);
        elements.push_back(bed);
        rendered += 10;
    }
    if (iObjects)
    {
        std::vector<ObjectSpec> const specs = Objects(iFrame);
        for (uint32_t j = 0; j < specs.size(); ++j)
        {
            IABObjectDefinition *object = new IABObjectDefinition(kIABFrameRate_24FPS);
            object->SetMetadataID(static_cast<IABMetadataIDType>(2 + j));
            object->SetAudioDataID(data(10 + j));
            object->SetConditionalObject(0);
            std::vector<IABObjectSubBlock *> subs;
            for (uint32_t s = 0; s < 8; ++s)
            {
                IABObjectSubBlock *sub = new IABObjectSubBlock();
                FillSubBlock(specs[j], *sub);
                subs.push_back(sub);
            }
            object->SetPanSubBlocks(subs);
            elements.push_back(object);
        }
        rendered += static_cast<uint32_t>(specs.size());
    }
    IABFrameInterface *frame = IABFrameInterface::Create(NULL);
    frame->SetSampleRate(iRate);
    frame->SetFrameRate(kIABFrameRate_24FPS);
    frame->SetMaxRendered(static_cast<IABMaxRenderedRangeType>(rendered));
    audio.insert(audio.end(), elements.begin(), elements.end());
    EXPECT_EQ(kIABNoError, frame->SetSubElements(audio));
    return frame;
}

// Renders three frames with a pool of iThreads and returns every output sample, frame after frame.
std::vector<float> Render(uint32_t iThreads, bool iBeds, bool iObjects, IABSampleRateType iRate, uint32_t iFrameSamples)
{
    std::unique_ptr<RenderUtils::IRendererConfiguration> config(
        RenderUtils::IRendererConfigurationFile::FromBuffer((char *)c916cfg.c_str()));
    EXPECT_TRUE(config != nullptr);
    IABRendererMTInterface *renderer = IABRendererMTInterface::Create(*config, iThreads, false);
    EXPECT_TRUE(renderer != nullptr);
    uint32_t const outputs = renderer->GetOutputChannelCount();
    std::vector<std::vector<float>> buffers(outputs, std::vector<float>(iFrameSamples));
    std::vector<float *> pointers(outputs);
    for (uint32_t c = 0; c < outputs; ++c)
    {
        pointers[c] = buffers[c].data();
    }
    std::vector<float> all;
    for (uint32_t f = 0; f < 3; ++f)
    {
        IABFrameInterface *frame = MakeFrame(f, iBeds, iObjects, iRate, iFrameSamples);
        EXPECT_EQ(kIABNoError, renderer->RenderIABFrame(*frame, pointers.data(), outputs, iFrameSamples));
        IABFrameInterface::Delete(frame);
        for (uint32_t c = 0; c < outputs; ++c)
        {
            all.insert(all.end(), buffers[c].begin(), buffers[c].end());
        }
    }
    IABRendererMTInterface::Delete(renderer);
    return all;
}

void ExpectSame(std::vector<float> const &iOne, std::vector<float> const &iMany, bool iExpectSignal)
{
    ASSERT_EQ(iOne.size(), iMany.size());
    double energy = 0;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < iOne.size(); ++i)
    {
        energy += double(iOne[i]) * iOne[i];
        if (std::fabs(iMany[i] - iOne[i]) > 1e-6f * std::max(1.0f, std::fabs(iOne[i])))
        {
            ++bad;
        }
    }
    EXPECT_EQ(0u, bad);
    EXPECT_EQ(iExpectSignal, energy > 0);
}

TEST(IABRendererMTAccumulation, SameAsOneThread)
{
    ExpectSame(Render(1, true, true, kIABSampleRate_48000Hz, 2000), Render(4, true, true, kIABSampleRate_48000Hz, 2000),
               true);
}

TEST(IABRendererMTAccumulation, BedsOnly)
{
    ExpectSame(Render(1, true, false, kIABSampleRate_48000Hz, 2000), Render(4, true, false, kIABSampleRate_48000Hz, 2000),
               true);
}

TEST(IABRendererMTAccumulation, EmptyFrame)
{
    ExpectSame(Render(1, false, false, kIABSampleRate_48000Hz, 2000),
               Render(4, false, false, kIABSampleRate_48000Hz, 2000), false);
}

// The decorrelated object reaches the outputs: muting it changes them. (Both sides of SameAsOneThread would lose it
// alike if the decorrelation outputs were not added.)
TEST(IABRendererMTAccumulation, DecorrelatedObjectIsHeard)
{
    std::vector<float> const heard = Render(4, false, true, kIABSampleRate_48000Hz, 2000);
    gMuteDecorrelated = true;
    std::vector<float> const muted = Render(4, false, true, kIABSampleRate_48000Hz, 2000);
    gMuteDecorrelated = false;
    ASSERT_EQ(heard.size(), muted.size());
    double difference = 0;
    for (std::size_t i = 0; i < heard.size(); ++i)
    {
        difference += double(heard[i] - muted[i]) * (heard[i] - muted[i]);
    }
    EXPECT_GT(difference, 1e-3);
}

TEST(IABRendererMTAccumulation, At96k)
{
    ExpectSame(Render(1, true, true, kIABSampleRate_96000Hz, 4000), Render(4, true, true, kIABSampleRate_96000Hz, 4000),
               true);
}

} // namespace
