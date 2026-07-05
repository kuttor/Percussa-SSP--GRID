// SSPApi.cpp — Bridge between SSP host (SYNTHOR) and GRID plugin.

#include <Percussa.h>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace grid;

enum SSPButtons {
    SSP_Soft_1, SSP_Soft_2, SSP_Soft_3, SSP_Soft_4,
    SSP_Soft_5, SSP_Soft_6, SSP_Soft_7, SSP_Soft_8,
    SSP_Left, SSP_Right, SSP_Up, SSP_Down,
    SSP_Shift_L, SSP_Shift_R,
    SSP_LastBtn
};

#define SSP_IMAGECACHE_HASHCODE 0x4752494448415348

// ─── Editor Interface ────────────────────────────────────────────────────

class SSP_PluginEditorInterface : public Percussa::SSP::PluginEditorInterface {
public:
    explicit SSP_PluginEditorInterface(PluginEditor *editor)
        : editor_(editor) {}

    ~SSP_PluginEditorInterface() override {
        if (editor_) delete editor_;
    }

    void frameStart() override {
        PluginEditorInterface::frameStart();
        if (editor_) editor_->timerCallback();
    }

    void visibilityChanged(bool b) override {
        PluginEditorInterface::visibilityChanged(b);
    }

    void renderToImage(unsigned char *buffer, int width, int height) override {
        if (!editor_) return;
        juce::Image img = juce::ImageCache::getFromHashCode(SSP_IMAGECACHE_HASHCODE);
        if (!img.isValid()) {
            juce::Image newimg(juce::Image::ARGB, width, height, true);
            juce::ImageCache::addImageToCache(newimg, SSP_IMAGECACHE_HASHCODE);
            img = newimg;
        }

        if (!editor_->isVisible()) {
            editor_->setBounds(juce::Rectangle<int>(0, 0, width, height));
            editor_->setOpaque(true);
            editor_->setVisible(true);
        }

        juce::Graphics g(img);
        editor_->paintEntireComponent(g, true);
        juce::Image::BitmapData bitmap(img, juce::Image::BitmapData::readOnly);
        memcpy(buffer, bitmap.data, static_cast<size_t>(width * height * 4));
    }

    void buttonPressed(int n, bool val) {
        if (n <= SSP_Soft_8) {
            editor_->onButton(n, val);
        } else {
            switch (n) {
                case SSP_Left:    editor_->onLeftButton(val); break;
                case SSP_Right:   editor_->onRightButton(val); break;
                case SSP_Up:      editor_->onUpButton(val); break;
                case SSP_Down:    editor_->onDownButton(val); break;
                case SSP_Shift_L: editor_->onLeftShiftButton(val); break;
                case SSP_Shift_R: editor_->onRightShiftButton(val); break;
                default: break;
            }
        }
    }

    void encoderPressed(int n, bool val) {
        editor_->onEncoderSwitch(n, val);
    }

    void encoderTurned(int n, int val) {
        editor_->onEncoder(n, static_cast<float>(val));
    }

private:
    PluginEditor *editor_;
};

// ─── Plugin Interface ────────────────────────────────────────────────────

class SSP_PluginInterface : public Percussa::SSP::PluginInterface {
public:
    explicit SSP_PluginInterface(PluginProcessor *p)
        : processor_(p) {}

    ~SSP_PluginInterface() override {
        // Signal audio thread to stop before tearing down
        destroying_.store(true, std::memory_order_release);
        if (editor_) delete editor_;
        if (processor_) delete processor_;
    }

    Percussa::SSP::PluginEditorInterface *getEditor() override {
        if (editor_ == nullptr) {
            auto *pluginEditor = static_cast<PluginEditor *>(processor_->createEditor());
            editor_ = new SSP_PluginEditorInterface(pluginEditor);
        }
        return editor_;
    }

    void buttonPressed(int n, bool val) override {
        if (editor_) editor_->buttonPressed(n, val);
    }
    void encoderPressed(int n, bool val) override {
        if (editor_) editor_->encoderPressed(n, val);
    }
    void encoderTurned(int n, int val) override {
        if (editor_) editor_->encoderTurned(n, val);
    }
    void inputEnabled(int n, bool val) override {
        processor_->onInputChanged(n, val);
    }
    void outputEnabled(int n, bool val) override {
        processor_->onOutputChanged(n, val);
    }

    void getState(void **buffer, size_t *size) override {
        if (!buffer || !size || !processor_) {
            if (size) *size = 0;
            if (buffer) *buffer = nullptr;
            return;
        }
        try {
            juce::MemoryBlock state;
            processor_->getStateInformation(state);
            *size = state.getSize();
            if (*size > 0) {
                // Use malloc — safer than new char[] for C-style host API
                *buffer = std::malloc(*size);
                if (*buffer) {
                    state.copyTo(*buffer, 0, *size);
                } else {
                    *size = 0;
                }
            } else {
                *buffer = nullptr;
            }
        } catch (...) {
            *size = 0;
            *buffer = nullptr;
        }
    }

    void setState(void *buffer, size_t size) override {
        if (!buffer || size == 0 || !processor_) return;
        try {
            processor_->setStateInformation(buffer, static_cast<int>(size));
        } catch (...) {}
    }

    void prepare(double sampleRate, int samplesPerBlock) override {
        processor_->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
        processor_->prepareToPlay(sampleRate, samplesPerBlock);
    }

    void process(float **channelData, int numChannels, int numSamples) override {
        // Guard against calls during destruction
        if (destroying_.load(std::memory_order_acquire)) {
            for (int ch = 0; ch < numChannels; ++ch)
                if (channelData[ch])
                    std::memset(channelData[ch], 0, sizeof(float) * numSamples);
            return;
        }
        juce::MidiBuffer midiBuffer;
        juce::AudioSampleBuffer buffer(channelData, numChannels, numSamples);
        processor_->processBlock(buffer, midiBuffer);
    }

private:
    SSP_PluginEditorInterface *editor_ = nullptr;
    PluginProcessor *processor_ = nullptr;
    std::atomic<bool> destroying_ { false };
};

// ─── Exported C Functions ────────────────────────────────────────────────

extern "C" __attribute__((visibility("default")))
Percussa::SSP::PluginDescriptor *createDescriptor() {
    auto *desc = new Percussa::SSP::PluginDescriptor;
    desc->name = JucePlugin_Name;
    desc->descriptiveName = JucePlugin_Desc;
    desc->manufacturerName = JucePlugin_Manufacturer;
    desc->version = JucePlugin_VersionString;
    desc->uid = static_cast<int>(JucePlugin_VSTUniqueID);

    auto busProps = PluginProcessor::getDefaultBusesProperties();
    for (auto& layout : busProps.inputLayouts)
        desc->inputChannelNames.push_back(layout.busName.toStdString());
    for (auto& layout : busProps.outputLayouts)
        desc->outputChannelNames.push_back(layout.busName.toStdString());

    desc->colour = 0xFFE53935;

    return desc;
}

extern "C" __attribute__((visibility("default")))
Percussa::SSP::PluginInterface *createInstance() {
    return new SSP_PluginInterface(new PluginProcessor());
}

extern "C" __attribute__((visibility("default")))
void getApiVersion(unsigned &major, unsigned &minor) {
    major = Percussa::SSP::API_MAJOR_VERSION;
    minor = Percussa::SSP::API_MINOR_VERSION;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new grid::PluginProcessor();
}
