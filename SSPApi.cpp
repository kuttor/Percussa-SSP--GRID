// SSPApi.cpp — Bridge between SSP host (SYNTHOR) and GRID plugin.

#include <Percussa.h>
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
        editor_->timerCallback();
    }

    void visibilityChanged(bool b) override {
        PluginEditorInterface::visibilityChanged(b);
    }

    void renderToImage(unsigned char *buffer, int width, int height) override {
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
        juce::MemoryBlock state;
        processor_->getStateInformation(state);
        *size = state.getSize();
        *buffer = new char[*size];
        state.copyTo(*buffer, 0, *size);
    }
    void setState(void *buffer, size_t size) override {
        processor_->setStateInformation(buffer, static_cast<int>(size));
    }

    void prepare(double sampleRate, int samplesPerBlock) override {
        // Bear's pattern: setRateAndBufferSizeDetails, NOT setPlayConfigDetails
        processor_->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
        processor_->prepareToPlay(sampleRate, samplesPerBlock);
    }

    void process(float **channelData, int numChannels, int numSamples) override {
        juce::MidiBuffer midiBuffer;
        juce::AudioSampleBuffer buffer(channelData, numChannels, numSamples);
        processor_->processBlock(buffer, midiBuffer);
    }

private:
    SSP_PluginEditorInterface *editor_ = nullptr;
    PluginProcessor *processor_ = nullptr;
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

    // Bear's pattern: read bus names from BusesProperties
    auto busProps = PluginProcessor::getDefaultBusesProperties();
    for (auto& layout : busProps.inputLayouts)
        desc->inputChannelNames.push_back(layout.busName.toStdString());
    for (auto& layout : busProps.outputLayouts)
        desc->outputChannelNames.push_back(layout.busName.toStdString());

    desc->colour = 0xFFE53935;  // ELAS red

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

// JUCE VST3 wrapper needs this — placed here because this file's symbols
// are guaranteed to link into the VST3 target
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new grid::PluginProcessor();
}
