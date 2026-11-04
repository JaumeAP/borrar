// IABRendererMT::RenderFloatFrame renders a float frame as RenderIABFrame renders the IAB frame with the same values:
// objects with position, gain, spread, snap, zone gains and decorrelation, and a bed, over several frames (the gain
// smoothing across frames), at 24 and 25 fps.

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

IABChannelIDType const kBed[10] = {kIABChannelID_Left, kIABChannelID_Center, kIABChannelID_Right,
                                   kIABChannelID_LeftSideSurround, kIABChannelID_LeftRearSurround,
                                   kIABChannelID_RightRearSurround, kIABChannelID_RightSideSurround, kIABChannelID_LFE,
                                   kIABChannelID_LeftTopSurround, kIABChannelID_RightTopSurround};

uint32_t const kObjects = 12;

// The pan of object iObject in sub-block iSub of frame iFrame: moving positions, one with spread, one with snap, one
// with zone gains, one decorrelated, gains below 1.
IABFloatPan Pan(uint32_t iFrame, uint32_t iObject, uint32_t iSub)
{
    IABFloatPan pan;
    float const t = 0.01f * static_cast<float>(iFrame * 8 + iSub);
    pan.x = std::fmod(0.07f * iObject + t, 1.0f);
    pan.y = std::fmod(0.11f * iObject + 0.5f * t, 1.0f);
    pan.z = (iObject % 3) * 0.5f;
    pan.gain = 0.4f + 0.05f * iObject;
    pan.spreadX = pan.spreadY = pan.spreadZ = iObject == 1 ? 0.3f : 0.0f;
    pan.snap = iObject == 2;
    pan.snapTolerance = iObject == 2 ? 0.1f : 0.0f;
    pan.zoneControl = iObject == 3;
    for (int z = 0; z < 9; ++z)
    {
        pan.zoneGains[z] = z % 2 == 0 ? 1.0f : 0.25f;
    }
    pan.decorrelate = iObject == 4;
    return pan;
}

float Sample(uint32_t iFrame, uint32_t iChannel, uint32_t iSample, uint32_t iFrameSamples)
{
    double const n = double(iFrame) * iFrameSamples + iSample;
    return static_cast<float>(0.1 * std::sin(n * (0.001 + 0.0007 * iChannel)));
}

// The same pan as an IAB sub-block (the setters FillObjectSubBlock uses, plus the snap tolerance).
void FillSubBlock(IABFloatPan const &iPan, IABObjectSubBlock &oSub)
{
    IABGain gain;
    gain.setIABGain(iPan.gain);
    CartesianPosInUnitCube point;
    point.setIABObjectPosition(iPan.x, iPan.y, iPan.z);
    IABObjectSnap snap = IABObjectSnap();
    snap.objectSnapPresent_ = iPan.snap ? 1 : 0;
    snap.objectSnapTolerance_ = static_cast<uint16_t>(iPan.snapTolerance * 4095.0f + 0.5f);
    IABObjectZoneGain9 zones = IABObjectZoneGain9();
    zones.objectZoneControl_ = iPan.zoneControl ? 1 : 0;
    for (int z = 0; z < 9 && iPan.zoneControl; ++z)
    {
        zones.zoneGains_[z].setIABZoneGain(iPan.zoneGains[z]);
    }
    IABObjectSpread spread;
    spread.setIABObjectSpread(iPan.spreadMode, iPan.spreadX, iPan.spreadY, iPan.spreadZ);
    IABDecorCoeff decor = IABDecorCoeff();
    decor.decorCoefPrefix_ = iPan.decorrelate ? kIABDecorCoeffPrefix_MaxDecor : kIABDecorCoeffPrefix_NoDecor;
    oSub.SetPanInfoExists(1);
    oSub.SetObjectGain(gain);
    oSub.SetObjectPositionFromUnitCube(point);
    oSub.SetObjectSnap(snap);
    oSub.SetObjectZoneGains9(zones);
    oSub.SetObjectSpread(spread);
    oSub.SetDecorCoef(decor);
}

struct Renders
{
    std::vector<float> iab, floats;
};

// Renders three frames both ways, each with its own renderer of one worker thread.
Renders RenderBoth(IABFrameRateType iFrameRate, uint32_t iFrameSamples)
{
    std::unique_ptr<RenderUtils::IRendererConfiguration> config(
        RenderUtils::IRendererConfigurationFile::FromBuffer((char *)c916cfg.c_str()));
    IABRendererMTInterface *iabRenderer = IABRendererMTInterface::Create(*config, 1, false);
    IABRendererMTInterface *floatRenderer = IABRendererMTInterface::Create(*config, 1, false);
    uint32_t const outputs = iabRenderer->GetOutputChannelCount();
    std::vector<std::vector<float>> buffers(outputs, std::vector<float>(iFrameSamples));
    std::vector<float *> pointers(outputs);
    for (uint32_t c = 0; c < outputs; ++c)
    {
        pointers[c] = buffers[c].data();
    }
    Renders result;
    std::vector<std::vector<float>> audio(10 + kObjects, std::vector<float>(iFrameSamples));
    for (uint32_t f = 0; f < 3; ++f)
    {
        for (uint32_t ch = 0; ch < audio.size(); ++ch)
        {
            for (uint32_t s = 0; s < iFrameSamples; ++s)
            {
                audio[ch][s] = Sample(f, ch, s, iFrameSamples);
            }
        }

        // The IAB frame
        std::vector<IABElement *> elements, definitions;
        IABAudioDataIDType nextId = 1;
        auto data = [&](uint32_t iChannel) {
            IABAudioDataFloat *element = new IABAudioDataFloat(iFrameSamples);
            element->SetAudioDataID(nextId);
            element->SetSamples(audio[iChannel].data(), iFrameSamples);
            elements.push_back(element);
            return nextId++;
        };
        IABBedDefinition *bed = new IABBedDefinition(iFrameRate);
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
        definitions.push_back(bed);
        for (uint32_t j = 0; j < kObjects; ++j)
        {
            IABObjectDefinition *object = new IABObjectDefinition(iFrameRate);
            object->SetMetadataID(static_cast<IABMetadataIDType>(2 + j));
            object->SetAudioDataID(data(10 + j));
            object->SetConditionalObject(0);
            std::vector<IABObjectSubBlock *> subs;
            for (uint32_t s = 0; s < 8; ++s)
            {
                IABObjectSubBlock *sub = new IABObjectSubBlock();
                FillSubBlock(Pan(f, j, s), *sub);
                subs.push_back(sub);
            }
            object->SetPanSubBlocks(subs);
            definitions.push_back(object);
        }
        IABFrameInterface *frame = IABFrameInterface::Create(NULL);
        frame->SetSampleRate(kIABSampleRate_48000Hz);
        frame->SetFrameRate(iFrameRate);
        frame->SetMaxRendered(static_cast<IABMaxRenderedRangeType>(10 + kObjects));
        elements.insert(elements.end(), definitions.begin(), definitions.end());
        EXPECT_EQ(kIABNoError, frame->SetSubElements(elements));
        EXPECT_EQ(kIABNoError, iabRenderer->RenderIABFrame(*frame, pointers.data(), outputs, iFrameSamples));
        IABFrameInterface::Delete(frame);
        for (uint32_t c = 0; c < outputs; ++c)
        {
            result.iab.insert(result.iab.end(), buffers[c].begin(), buffers[c].end());
        }

        // The float frame with the same values
        IABFloatFrame floatFrame;
        floatFrame.sampleRate = kIABSampleRate_48000Hz;
        floatFrame.frameRate = iFrameRate;
        IABFloatBed floatBed;
        floatBed.metadataId = 1;
        for (uint32_t c = 0; c < 10; ++c)
        {
            IABFloatBedChannel channel;
            channel.channelId = kBed[c];
            channel.samples = audio[c].data();
            floatBed.channels.push_back(channel);
        }
        floatFrame.beds.push_back(floatBed);
        for (uint32_t j = 0; j < kObjects; ++j)
        {
            IABFloatObject object;
            object.metadataId = static_cast<IABMetadataIDType>(2 + j);
            object.samples = audio[10 + j].data();
            for (uint32_t s = 0; s < 8; ++s)
            {
                object.pans.push_back(Pan(f, j, s));
            }
            floatFrame.objects.push_back(object);
        }
        EXPECT_EQ(kIABNoError, floatRenderer->RenderFloatFrame(floatFrame, pointers.data(), outputs, iFrameSamples));
        for (uint32_t c = 0; c < outputs; ++c)
        {
            result.floats.insert(result.floats.end(), buffers[c].begin(), buffers[c].end());
        }
    }
    IABRendererMTInterface::Delete(iabRenderer);
    IABRendererMTInterface::Delete(floatRenderer);
    return result;
}

void ExpectSame(Renders const &iRenders)
{
    ASSERT_EQ(iRenders.iab.size(), iRenders.floats.size());
    double energy = 0;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < iRenders.iab.size(); ++i)
    {
        energy += double(iRenders.iab[i]) * iRenders.iab[i];
        if (std::fabs(iRenders.floats[i] - iRenders.iab[i]) > 1e-6f * std::max(1.0f, std::fabs(iRenders.iab[i])))
        {
            ++bad;
        }
    }
    EXPECT_EQ(0u, bad);
    EXPECT_GT(energy, 0.0);
}

TEST(IABRendererMTFloatFrame, SameAsIabFrame)
{
    ExpectSame(RenderBoth(kIABFrameRate_24FPS, 2000));
}

TEST(IABRendererMTFloatFrame, At25fps)
{
    ExpectSame(RenderBoth(kIABFrameRate_25FPS, 1920));
}

// A pan count other than the frame rate's sub-blocks is refused.
TEST(IABRendererMTFloatFrame, WrongPanCount)
{
    std::unique_ptr<RenderUtils::IRendererConfiguration> config(
        RenderUtils::IRendererConfigurationFile::FromBuffer((char *)c916cfg.c_str()));
    IABRendererMTInterface *renderer = IABRendererMTInterface::Create(*config, 1, false);
    uint32_t const outputs = renderer->GetOutputChannelCount();
    std::vector<std::vector<float>> buffers(outputs, std::vector<float>(2000));
    std::vector<float *> pointers(outputs);
    for (uint32_t c = 0; c < outputs; ++c)
    {
        pointers[c] = buffers[c].data();
    }
    std::vector<float> samples(2000, 0.1f);
    IABFloatFrame frame;
    IABFloatObject object;
    object.metadataId = 1;
    object.samples = samples.data();
    object.pans.resize(4);
    frame.objects.push_back(object);
    EXPECT_NE(kIABNoError, renderer->RenderFloatFrame(frame, pointers.data(), outputs, 2000));
    IABRendererMTInterface::Delete(renderer);
}

} // namespace
