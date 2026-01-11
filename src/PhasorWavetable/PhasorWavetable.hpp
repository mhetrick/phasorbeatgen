#pragma once
#include "../PhasorBeatMapPlugin.hpp"
#include "../HetrickUtilities.hpp"
#include "../DSP/Phasors/HCVPhasorCommon.h"
#include <rack.hpp>
#include <osdialog.h>
#include "PhasorWavetable/dr_wav.h"
#include <thread>
#include <cmath>

using namespace rack;

static const char WAVETABLE_LOAD_FILTERS[] = "WAV:wav,WAV,aif,AIF,aiff,AIFF,f32,s8,i8,s16,i16,s24,i24,s32,i32";
static const char WAVETABLE_SAVE_FILTERS[] = "WAV:wav,WAV";
static std::string wavetableDir;

/// Wavetable structure with mipmap support for antialiasing
/// Supports user-selectable wave lengths and dynamic mipmap generation
struct PhasorWavetableData {
    /// All waves concatenated (waveCount, waveLen)
    std::vector<float> samples;
    /// Number of points in each wave
    size_t waveLen = 0;
    /// Name of loaded wavetable
    std::string filename;

    // Mipmap data for antialiasing (VCO mode)
    /// Upsampling factor. No upsampling if 0.
    size_t quality = 0;
    /// Number of filtered wavetables (octaves). Automatically computed from waveLen.
    size_t octaves = 0;
    /// Waves bandlimited at each octave (octave, waveCount, waveLen * quality)
    std::vector<float> interpolatedSamples;

    bool loading = false;

    PhasorWavetableData() {}

    float& at(size_t waveIndex, size_t sampleIndex) {
        return samples[waveLen * waveIndex + sampleIndex];
    }

    float at(size_t waveIndex, size_t sampleIndex) const {
        return samples[waveLen * waveIndex + sampleIndex];
    }

    float interpolatedAt(size_t octave, size_t waveIndex, size_t sampleIndex) const {
        return interpolatedSamples[samples.size() * quality * octave + waveLen * quality * waveIndex + sampleIndex];
    }

    void reset() {
        filename = "Basic.wav";
        waveLen = 1024;
        loading = true;
        DEFER({loading = false;});
        // HACK Sleep 100us so DSP thread is likely to finish processing before we resize the vector
        std::this_thread::sleep_for(std::chrono::duration<double>(100e-6));
        samples.resize(waveLen * 4);

        // Sine
        for (size_t i = 0; i < waveLen; i++) {
            float p = float(i) / waveLen;
            at(0, i) = std::sin(2 * float(M_PI) * p);
        }
        // Triangle
        for (size_t i = 0; i < waveLen; i++) {
            float p = float(i) / waveLen;
            at(1, i) = (p < 0.25f) ? 4*p : (p < 0.75f) ? 2 - 4*p : 4*p - 4;
        }
        // Sawtooth
        for (size_t i = 0; i < waveLen; i++) {
            float p = float(i) / waveLen;
            at(2, i) = (p < 0.5f) ? 2*p : 2*p - 2;
        }
        // Square
        for (size_t i = 0; i < waveLen; i++) {
            float p = float(i) / waveLen;
            at(3, i) = (p < 0.5f) ? 1 : -1;
        }
        interpolate();
    }

    void setQuality(size_t quality) {
        if (quality == this->quality)
            return;
        this->quality = quality;
        interpolate();
    }

    void setWaveLen(size_t waveLen) {
        if (waveLen == this->waveLen)
            return;
        this->waveLen = waveLen;
        interpolate();
    }

    size_t getWaveCount() const {
        if (waveLen == 0)
            return 0;
        return samples.size() / waveLen;
    }

    /// Generate mipmap levels using FFT-based filtering
    void interpolate() {
        if (quality == 0)
            return;
        // pffft only supports >=32 points
        if (waveLen < 32)
            return;
        // pffft only supports multiples of 32 points
        if ((waveLen % 32) != 0)
            return;

        size_t waveCount = getWaveCount();
        if (waveCount == 0)
            return;

        octaves = math::log2(waveLen) - 1;
        interpolatedSamples.clear();
        interpolatedSamples.resize(octaves * samples.size() * quality);

        float* in = new float[waveLen];
        float* inF = new float[2 * waveLen];
        dsp::RealFFT inFFT(waveLen);

        float* outF = new float[2 * waveLen * quality]();
        dsp::RealFFT outFFT(waveLen * quality);

        for (size_t i = 0; i < waveCount; i++) {
            // Compute FFT of wave
            for (size_t j = 0; j < waveLen; j++) {
                in[j] = samples[waveLen * i + j] / waveLen;
            }
            inFFT.rfft(in, inF);
            // Compute FFT-filtered versions of each wave
            for (size_t octave = 0; octave < octaves; octave++) {
                size_t bins = 1 << octave;
                // Only overwrite the first waveLen bins
                for (size_t j = 0; j < waveLen; j++) {
                    outF[2 * j + 0] = (j <= bins) ? inF[2 * j + 0] : 0.f;
                    outF[2 * j + 1] = (j <= bins) ? inF[2 * j + 1] : 0.f;
                }
                outFFT.irfft(outF, &interpolatedSamples[samples.size() * quality * octave + waveLen * quality * i]);
            }
        }

        delete[] in;
        delete[] inF;
        delete[] outF;
    }

    json_t* toJson() const {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "waveLen", json_integer(waveLen));
        json_object_set_new(rootJ, "filename", json_string(filename.c_str()));
        return rootJ;
    }

    void fromJson(json_t* rootJ) {
        json_t* waveLenJ = json_object_get(rootJ, "waveLen");
        if (waveLenJ)
            setWaveLen(json_integer_value(waveLenJ));
        json_t* filenameJ = json_object_get(rootJ, "filename");
        if (filenameJ)
            filename = json_string_value(filenameJ);
    }

    void load(std::string path) {
        loading = true;
        DEFER({loading = false;});
        std::this_thread::sleep_for(std::chrono::duration<double>(100e-6));

        std::string ext = string::lowercase(system::getExtension(path));
        if (ext == ".wav") {
            drwav wav;
#if defined ARCH_WIN
            if (!drwav_init_file_w(&wav, string::UTF8toUTF16(path).c_str(), NULL))
#else
            if (!drwav_init_file(&wav, path.c_str(), NULL))
#endif
                return;

            size_t len = wav.totalPCMFrameCount * wav.channels;
            if (len == 0 || len >= (1 << 20))
                return;

            samples.clear();
            samples.resize(len);

            // If sample rate is a power of 2, set waveLen to it.
            if ((wav.sampleRate & (wav.sampleRate - 1)) == 0)
                waveLen = wav.sampleRate;

            drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, samples.data());
            drwav_uninit(&wav);
        }
        else {
            std::vector<uint8_t> data = system::readFile(path);
            samples.clear();

            if (ext == ".f32") {
                size_t len = data.size() / sizeof(float);
                samples.resize(len);
                dsp::convert((const float*) data.data(), samples.data(), len);
            }
            else if (ext == ".s8" || ext == ".i8") {
                size_t len = data.size() / sizeof(int8_t);
                samples.resize(len);
                dsp::convert((const int8_t*) data.data(), samples.data(), len);
            }
            else if (ext == ".s16" || ext == ".i16") {
                size_t len = data.size() / sizeof(int16_t);
                samples.resize(len);
                dsp::convert((const int16_t*) data.data(), samples.data(), len);
            }
            else if (ext == ".s24" || ext == ".i24") {
                size_t len = data.size() / sizeof(dsp::Int24);
                samples.resize(len);
                dsp::convert((const dsp::Int24*) data.data(), samples.data(), len);
            }
            else if (ext == ".s32" || ext == ".i32") {
                size_t len = data.size() / sizeof(int32_t);
                samples.resize(len);
                dsp::convert((const int32_t*) data.data(), samples.data(), len);
            }
            else {
                return;
            }
        }

        interpolate();
    }

    void loadDialog() {
        osdialog_filters* filters = osdialog_filters_parse(WAVETABLE_LOAD_FILTERS);
        DEFER({osdialog_filters_free(filters);});

        char* pathC = osdialog_file(OSDIALOG_OPEN, wavetableDir.empty() ? NULL : wavetableDir.c_str(), NULL, filters);
        if (!pathC) {
            return;
        }
        std::string path = pathC;
        std::free(pathC);
        wavetableDir = system::getDirectory(path);

        load(path);
        filename = system::getFilename(path);
    }

    void save(std::string path) const {
        if (samples.size() == 0)
            return;

        drwav_data_format format;
        format.container = drwav_container_riff;
        format.format = DR_WAVE_FORMAT_PCM;
        format.channels = 1;
        format.sampleRate = waveLen;
        format.bitsPerSample = 16;

        drwav wav;
        if (!drwav_init_file_write(&wav, path.c_str(), &format, NULL))
            return;

        int16_t* buf = new int16_t[samples.size()];
        drwav_f32_to_s16(buf, samples.data(), samples.size());
        drwav_write_pcm_frames(&wav, samples.size(), buf);
        delete[] buf;

        drwav_uninit(&wav);
    }

    void saveDialog() const {
        osdialog_filters* filters = osdialog_filters_parse(WAVETABLE_SAVE_FILTERS);
        DEFER({osdialog_filters_free(filters);});

        char* pathC = osdialog_file(OSDIALOG_SAVE, wavetableDir.empty() ? NULL : wavetableDir.c_str(), filename.c_str(), filters);
        if (!pathC) {
            return;
        }
        DEFER({std::free(pathC);});

        std::string path = pathC;
        if (system::getExtension(path) != ".wav") {
            path += ".wav";
        }
        wavetableDir = system::getDirectory(path);

        save(path);
    }

    void appendContextMenu(Menu* menu) {
        menu->addChild(createMenuItem("Initialize wavetable", "",
            [=]() {reset();}
        ));

        menu->addChild(createMenuItem("Load wavetable", "",
            [=]() {loadDialog();}
        ));

        menu->addChild(createMenuItem("Save wavetable", "",
            [=]() {saveDialog();}
        ));

        int sizeOffset = 5;
        std::vector<std::string> sizeLabels;
        for (int i = sizeOffset; i <= 14; i++) {
            sizeLabels.push_back(string::f("%d", 1 << i));
        }
        menu->addChild(createIndexSubmenuItem("Wave points", sizeLabels,
            [=]() {return math::log2(waveLen) - sizeOffset;},
            [=](int i) {waveLen = 1 << (i + sizeOffset);}
        ));
    }
};


static PhasorWavetableData defaultPhasorWavetable;


/// Display widget for wavetable visualization
template <class TModule>
struct PhasorWTDisplay : LedDisplay {
    TModule* module;

    void drawLayer(const DrawArgs& args, int layer) override {
        nvgScissor(args.vg, RECT_ARGS(args.clipBox));

        if (layer == 1) {
            if (defaultPhasorWavetable.samples.empty())
                defaultPhasorWavetable.reset();

            const PhasorWavetableData& wavetable = module ? module->wavetable : defaultPhasorWavetable;
            float lastPos = module ? module->lastPos : 0.f;

            // Draw filename text
            std::string fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf");
            std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
            if (!font)
                return;
            nvgFontSize(args.vg, 13);
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, SCHEME_YELLOW);
            nvgText(args.vg, 4.0, 13.0, wavetable.filename.c_str(), NULL);

            if (wavetable.waveLen < 2)
                return;

            size_t waveCount = wavetable.getWaveCount();
            if (waveCount < 1)
                return;
            if (lastPos > waveCount - 1)
                return;
            float posF = lastPos - std::trunc(lastPos);
            size_t pos0 = std::trunc(lastPos);

            // Draw scope
            nvgScissor(args.vg, RECT_ARGS(args.clipBox));
            nvgBeginPath(args.vg);
            Vec scopePos = Vec(0.0, 13.0);
            Rect scopeRect = Rect(scopePos, box.size - scopePos);
            scopeRect = scopeRect.shrink(Vec(4, 5));
            size_t iSkip = wavetable.waveLen / 128 + 1;

            for (size_t i = 0; i <= wavetable.waveLen; i += iSkip) {
                float wave;
                float wave0 = wavetable.at(pos0, i % wavetable.waveLen);
                if (posF > 0.f && pos0 + 1 < waveCount) {
                    float wave1 = wavetable.at(pos0 + 1, i % wavetable.waveLen);
                    wave = crossfade(wave0, wave1, posF);
                }
                else {
                    wave = wave0;
                }

                Vec p;
                p.x = float(i) / wavetable.waveLen;
                p.y = 0.5f - 0.5f * wave;
                p = scopeRect.pos + scopeRect.size * p;
                if (i == 0)
                    nvgMoveTo(args.vg, VEC_ARGS(p));
                else
                    nvgLineTo(args.vg, VEC_ARGS(p));
            }
            nvgLineCap(args.vg, NVG_ROUND);
            nvgMiterLimit(args.vg, 2.f);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStrokeColor(args.vg, SCHEME_YELLOW);
            nvgStroke(args.vg);
        }

        nvgResetScissor(args.vg);
        LedDisplay::drawLayer(args, layer);
    }

    void onPathDrop(const PathDropEvent& e) override {
        if (!module)
            return;
        if (e.paths.empty())
            return;
        std::string path = e.paths[0];
        std::string ext = string::lowercase(system::getExtension(path));
        if (ext != ".wav" && ext != ".f32" && ext != ".s8" && ext != ".i8" &&
            ext != ".s16" && ext != ".i16" && ext != ".s24" && ext != ".i24" &&
            ext != ".s32" && ext != ".i32")
            return;
        module->wavetable.load(path);
        module->wavetable.filename = system::getFilename(path);
        e.consume(this);
    }
};


/// Phasor-driven wavetable oscillator module
struct PhasorWavetable : HCVModule {
    enum ParamId {
        POS_PARAM,
        POS_CV_PARAM,
        OFFSET_PARAM,
        INVERT_PARAM,
        MODE_PARAM,  // 0 = LFO (no antialiasing), 1 = VCO (mipmap antialiasing)
        NUM_PARAMS
    };
    enum InputId {
        PHASOR_INPUT,
        POS_INPUT,
        NUM_INPUTS
    };
    enum OutputId {
        WAVE_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightId {
        ENUMS(PHASE_LIGHT, 3),
        OFFSET_LIGHT,
        INVERT_LIGHT,
        MODE_LIGHT,
        NUM_LIGHTS
    };

    PhasorWavetableData wavetable;

    float lastPos = 0.f;
    float lastPhasor[HCV_MAX_POLYPHONY] = {};

    dsp::ClockDivider lightDivider;
    dsp::BooleanTrigger offsetTrigger;
    dsp::BooleanTrigger invertTrigger;

    PhasorWavetable() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configParam(POS_PARAM, 0.f, 1.f, 0.f, "Wavetable position", "%", 0.f, 100.f);
        configParam(POS_CV_PARAM, -1.f, 1.f, 0.f, "Wavetable position CV", "%", 0.f, 100.f);
        getParamQuantity(POS_CV_PARAM)->randomizeEnabled = false;

        configSwitch(OFFSET_PARAM, 0.f, 1.f, 1.f, "Offset", {"Bipolar", "Unipolar"});
        configSwitch(INVERT_PARAM, 0.f, 1.f, 0.f, "Invert");
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"LFO (no antialiasing)", "VCO (antialiased)"});

        configInput(PHASOR_INPUT, "Phasor");
        configInput(POS_INPUT, "Wavetable position");

        configOutput(WAVE_OUTPUT, "Wavetable");

        configLight(PHASE_LIGHT, "Phase");

        // Default to quality 8 for VCO mode
        wavetable.setQuality(8);

        lightDivider.setDivision(16);
        onReset();
    }

    void onReset() override {
        wavetable.reset();
        for (int c = 0; c < HCV_MAX_POLYPHONY; c++) {
            lastPhasor[c] = 0.f;
        }
    }

    void onAdd(const AddEvent& e) override {
        std::string path = system::join(getPatchStorageDirectory(), "wavetable.wav");
        wavetable.load(path);
    }

    void onSave(const SaveEvent& e) override {
        if (!wavetable.samples.empty()) {
            std::string path = system::join(createPatchStorageDirectory(), "wavetable.wav");
            wavetable.save(path);
        }
    }

    /// Read wavetable with linear interpolation (LFO mode - no antialiasing)
    float getWaveLFO(float phase, float pos) {
        // phase is 0-1, scale to wavetable index
        float indexF = phase * wavetable.waveLen;
        indexF = std::fmod(indexF, (float)wavetable.waveLen);
        if (indexF < 0) indexF += wavetable.waveLen;

        float sampleFrac = indexF - std::trunc(indexF);
        size_t i0 = (size_t)std::trunc(indexF);
        size_t i1 = (i0 + 1) % wavetable.waveLen;

        float posF = pos - std::trunc(pos);
        size_t pos0 = (size_t)clamp(std::trunc(pos), 0.f, (float)(wavetable.getWaveCount() - 1));
        size_t pos1 = std::min(pos0 + 1, wavetable.getWaveCount() - 1);

        float out = crossfade(wavetable.at(pos0, i0), wavetable.at(pos0, i1), sampleFrac);
        if (posF > 0.f && pos1 > pos0) {
            float out1 = crossfade(wavetable.at(pos1, i0), wavetable.at(pos1, i1), sampleFrac);
            out = crossfade(out, out1, posF);
        }
        return out;
    }

    /// Read wavetable with mipmap antialiasing (VCO mode)
    float getWaveVCO(float phase, float pos, float octave) {
        if (wavetable.quality == 0 || wavetable.octaves == 0)
            return getWaveLFO(phase, pos);

        // phase is 0-1, scale to interpolated wavetable index
        float indexF = phase * wavetable.waveLen * wavetable.quality;
        indexF = std::fmod(indexF, (float)(wavetable.waveLen * wavetable.quality));
        if (indexF < 0) indexF += wavetable.waveLen * wavetable.quality;

        float sampleFrac = indexF - std::trunc(indexF);
        size_t i0 = (size_t)std::trunc(indexF);
        size_t i1 = (i0 + 1) % (wavetable.waveLen * wavetable.quality);

        float posF = pos - std::trunc(pos);
        size_t pos0 = (size_t)clamp(std::trunc(pos), 0.f, (float)(wavetable.getWaveCount() - 1));
        size_t pos1 = std::min(pos0 + 1, wavetable.getWaveCount() - 1);

        size_t octave0 = (size_t)clamp(std::trunc(octave), 0.f, (float)(wavetable.octaves - 1));

        float out = crossfade(wavetable.interpolatedAt(octave0, pos0, i0),
                              wavetable.interpolatedAt(octave0, pos0, i1), sampleFrac);
        if (posF > 0.f && pos1 > pos0) {
            float out1 = crossfade(wavetable.interpolatedAt(octave0, pos1, i0),
                                   wavetable.interpolatedAt(octave0, pos1, i1), sampleFrac);
            out = crossfade(out, out1, posF);
        }
        return out;
    }

    void process(const ProcessArgs& args) override {
        float posParam = params[POS_PARAM].getValue();
        float posCvParam = params[POS_CV_PARAM].getValue();
        bool offset = (params[OFFSET_PARAM].getValue() > 0.f);
        bool invert = (params[INVERT_PARAM].getValue() > 0.f);
        bool vcoMode = (params[MODE_PARAM].getValue() > 0.f);

        int channels = std::max(1, inputs[PHASOR_INPUT].getChannels());

        int waveCount = wavetable.getWaveCount();
        if (!wavetable.loading && wavetable.waveLen >= 2 && waveCount >= 1) {
            for (int c = 0; c < channels; c++) {
                // Get phasor input and normalize to 0-1
                float phasorIn = inputs[PHASOR_INPUT].getPolyVoltage(c);
                float phase = scaleAndWrapPhasor(phasorIn);

                // Calculate wavetable position
                float pos = posParam + inputs[POS_INPUT].getPolyVoltage(c) * posCvParam / 10.f;
                pos = clamp(pos, 0.f, 1.f);
                pos *= (waveCount - 1);

                if (c == 0)
                    lastPos = pos;

                float out;
                if (vcoMode) {
                    // Calculate frequency from phasor delta for mipmap selection
                    float phaseDelta = phase - lastPhasor[c];
                    lastPhasor[c] = phase;

                    // Handle wraparound
                    if (phaseDelta > 0.5f) phaseDelta -= 1.0f;
                    else if (phaseDelta < -0.5f) phaseDelta += 1.0f;

                    float absDelta = std::fabs(phaseDelta);
                    if (absDelta < 1e-7f) absDelta = 1e-7f;

                    // Calculate octave for mipmap selection
                    // Higher frequency = higher octave = fewer harmonics
                    float freq = absDelta * args.sampleRate;
                    freq = std::fmin(freq, args.sampleRate / 2.f);
                    float octave = std::log2(args.sampleRate / 2.f / std::fmax(freq, 1.f));
                    octave = clamp(octave, 0.f, (float)(wavetable.octaves - 1));

                    out = getWaveVCO(phase, pos, octave);
                }
                else {
                    out = getWaveLFO(phase, pos);
                    lastPhasor[c] = phase;
                }

                if (invert)
                    out *= -1.f;
                if (offset)
                    out += 1.f;

                outputs[WAVE_OUTPUT].setVoltage(out * 5.f, c);
            }
        }
        else {
            for (int c = 0; c < channels; c++) {
                outputs[WAVE_OUTPUT].setVoltage(0.f, c);
            }
        }

        outputs[WAVE_OUTPUT].setChannels(channels);

        // Light
        if (lightDivider.process()) {
            float phase = lastPhasor[0];
            if (channels == 1) {
                float b = 1.f - phase;
                lights[PHASE_LIGHT + 0].setSmoothBrightness(b, args.sampleTime * lightDivider.getDivision());
                lights[PHASE_LIGHT + 1].setSmoothBrightness(b, args.sampleTime * lightDivider.getDivision());
                lights[PHASE_LIGHT + 2].setBrightness(0.f);
            }
            else {
                lights[PHASE_LIGHT + 0].setBrightness(0.f);
                lights[PHASE_LIGHT + 1].setBrightness(0.f);
                lights[PHASE_LIGHT + 2].setBrightness(1.f);
            }
            lights[OFFSET_LIGHT].setBrightness(offset);
            lights[INVERT_LIGHT].setBrightness(invert);
            lights[MODE_LIGHT].setBrightness(vcoMode);
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_t* wavetableJ = wavetable.toJson();
        json_object_update(rootJ, wavetableJ);
        json_decref(wavetableJ);
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        wavetable.fromJson(rootJ);
    }
};
