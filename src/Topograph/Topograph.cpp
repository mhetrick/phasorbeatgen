//
// Topograph.cpp
// Author: Dale Johnson
// Contact: valley.audio.soft@gmail.com
// Date: 5/12/2017
//

#include "Topograph.hpp"

Topograph::Topograph() {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

    // Parameters
    configParam(Topograph::MAPX_PARAM, 0.0, 1.0, 0.0, "Pattern Map X");
    configParam(Topograph::MAPY_PARAM, 0.0, 1.0, 0.0, "Pattern Map Y");
    configParam(Topograph::CHAOS_PARAM, 0.0, 1.0, 0.0, "Pattern Chaos");
    configParam(Topograph::BD_DENS_PARAM, 0.0, 1.0, 0.5, "Channel 1 Density");
    configParam(Topograph::SN_DENS_PARAM, 0.0, 1.0, 0.5, "Channel 2 Density");
    configParam(Topograph::HH_DENS_PARAM, 0.0, 1.0, 0.5, "Channel 3 Density");

    // Inputs
    configInput(Topograph::PHASOR_INPUT, "Phasor");
    configInput(Topograph::MAPX_CV, "Map X CV");
    configInput(Topograph::MAPY_CV, "Map Y CV");
    configInput(Topograph::CHAOS_CV, "Chaos CV");
    configInput(Topograph::BD_FILL_CV, "Channel 1 Density CV");
    configInput(Topograph::SN_FILL_CV, "Channel 2 Density CV");
    configInput(Topograph::HH_FILL_CV, "Channel 3 Density CV");

    // Outputs
    configOutput(Topograph::BD_OUTPUT, "Channel 1");
    configOutput(Topograph::SN_OUTPUT, "Channel 2");
    configOutput(Topograph::HH_OUTPUT, "Channel 3");
    configOutput(Topograph::BD_ACC_OUTPUT, "Channel 1 Accent");
    configOutput(Topograph::SN_ACC_OUTPUT, "Channel 2 Accent");
    configOutput(Topograph::HH_ACC_OUTPUT, "Channel 3 Accent");

    // Initialize
    srand(time(NULL));
    stepDetector.setNumberSteps(32);

    for (int i = 0; i < 6; ++i) {
        drumTriggers[i] = Oneshot(0.001, APP->engine->getSampleRate());
    }
    for (int i = 0; i < 3; ++i) {
        drumLED[i] = Oneshot(0.1, APP->engine->getSampleRate());
    }
    panelStyle = 0;
}

json_t* Topograph::dataToJson() {
    json_t *rootJ = json_object();
    json_object_set_new(rootJ, "sequencerMode", json_integer(sequencerMode));
    json_object_set_new(rootJ, "triggerOutputMode", json_integer(triggerOutputMode));
    json_object_set_new(rootJ, "accOutputMode", json_integer(accOutputMode));
    json_object_set_new(rootJ, "panelStyle", json_integer(panelStyle));
    return rootJ;
}

void Topograph::dataFromJson(json_t* rootJ) {
    json_t *sequencerModeJ = json_object_get(rootJ, "sequencerMode");
    if (sequencerModeJ) {
        sequencerMode = (Topograph::SequencerMode) json_integer_value(sequencerModeJ);
        inEuclideanMode = 0;
        switch (sequencerMode) {
            case HENRI:
                patternGenerator.setPatternMode(PATTERN_HENRI);
                break;
            case ORIGINAL:
                patternGenerator.setPatternMode(PATTERN_ORIGINAL);
                break;
            case EUCLIDEAN:
                patternGenerator.setPatternMode(PATTERN_EUCLIDEAN);
                inEuclideanMode = 1;
                break;
        }
        barCache.needsRegeneration = true;
	}

    json_t* triggerOutputModeJ = json_object_get(rootJ, "triggerOutputMode");
	if (triggerOutputModeJ) {
		triggerOutputMode = (Topograph::TriggerOutputMode) json_integer_value(triggerOutputModeJ);
	}

    json_t* accOutputModeJ = json_object_get(rootJ, "accOutputMode");
	if (accOutputModeJ) {
		accOutputMode = (Topograph::AccOutputMode) json_integer_value(accOutputModeJ);
        switch (accOutputMode) {
            case INDIVIDUAL_ACCENTS:
                patternGenerator.setAccentAltMode(false);
                break;
            case ACC_CLK_RST:
                patternGenerator.setAccentAltMode(true);
        }
        barCache.needsRegeneration = true;
	}

    json_t* panelStyleJ = json_object_get(rootJ, "panelStyle");
    if (panelStyleJ) {
        panelStyle = (int)json_integer_value(panelStyleJ);
    }
}

void Topograph::process(const ProcessArgs &args) {
    // Read pattern parameters
    mapX = clamp(params[MAPX_PARAM].getValue() + inputs[MAPX_CV].getVoltage() / 10.f, 0.f, 1.f);
    mapY = clamp(params[MAPY_PARAM].getValue() + inputs[MAPY_CV].getVoltage() / 10.f, 0.f, 1.f);
    chaos = clamp(params[CHAOS_PARAM].getValue() + inputs[CHAOS_CV].getVoltage() / 10.f, 0.f, 1.f);
    BDFill = clamp(params[BD_DENS_PARAM].getValue() + inputs[BD_FILL_CV].getVoltage() / 10.f, 0.f, 1.f);
    SNFill = clamp(params[SN_DENS_PARAM].getValue() + inputs[SN_FILL_CV].getVoltage() / 10.f, 0.f, 1.f);
    HHFill = clamp(params[HH_DENS_PARAM].getValue() + inputs[HH_FILL_CV].getVoltage() / 10.f, 0.f, 1.f);

    // Set pattern generator parameters
    patternGenerator.setMapX(mapX);
    patternGenerator.setMapY(mapY);
    patternGenerator.setBDDensity(BDFill);
    patternGenerator.setSDDensity(SNFill);
    patternGenerator.setHHDensity(HHFill);
    patternGenerator.setRandomness(chaos);
    patternGenerator.setEuclideanLength(0, mapX);
    patternGenerator.setEuclideanLength(1, mapY);
    patternGenerator.setEuclideanLength(2, chaos);

    // Check if bar needs regeneration due to parameter changes
    if (checkBarRegenerationNeeded()) {
        patternGenerator.generateBar(barCache);
    }

    // Process phasor input
    float phasor = inputs[PHASOR_INPUT].getVoltage() / 10.0f;  // Normalize to 0-1
    phasor = clamp(phasor, 0.f, 1.f);

    // Detect phasor resets and regenerate bar if chaos is active
    if (resetDetector.detectProportionalReset(phasor)) {
        if (chaos > 0.0f && !freezeActive) {
            patternGenerator.generateBar(barCache);
        }
    }

    // Feed to step detector
    stepDetector(phasor);

    // Check for step changes
    if (stepDetector.getStepChangedThisSample()) {
        int currentStep = stepDetector.getCurrentStep();
        triggerStepOutputs(currentStep);
        lastStep = currentStep;
    }

    // Update trigger outputs
    for (int i = 0; i < 6; ++i) {
        drumTriggers[i].process();
        outputs[outIDs[i]].setVoltage(drumTriggers[i].getState() ? 10.0f : 0.0f);
    }

    // Update UI
    updateUI();
}

void Topograph::updateUI() {
    for(int i = 0; i < 3; ++i) {
        drumLED[i].process();
        lights[drumLEDIds[i]].value = drumLED[i].getState() ? 1.0f : 0.0f;
    }
}

// Trigger outputs for a specific step based on cached bar data
void Topograph::triggerStepOutputs(int step) {
    step = clamp(step, 0, 31);
    const BarCache::StepData& data = barCache.steps[step];

    for (int i = 0; i < 3; ++i) {
        if (data.trigger[i]) {
            drumTriggers[i].trigger();
            drumLED[i].trigger();

            // Trigger accent output if accent is set
            if (data.accent[i]) {
                drumTriggers[i + 3].trigger();
            }
        }
    }
}

void Topograph::onSampleRateChange() {
    for(int i = 0; i < 3; ++i) {
        drumLED[i].setSampleRate(APP->engine->getSampleRate());
    }
    for(int i = 0; i < 6; ++i) {
        drumTriggers[i].setSampleRate(APP->engine->getSampleRate());
    }
}

void Topograph::onReset(const ResetEvent& e) {
    Module::onReset(e);
    barCache.needsRegeneration = true;
}

void Topograph::onRandomize(const RandomizeEvent& e) {
    params[Topograph::MAPX_PARAM].setValue(random::uniform());
    params[Topograph::MAPY_PARAM].setValue(random::uniform());
    params[Topograph::CHAOS_PARAM].setValue(random::uniform());
    params[Topograph::BD_DENS_PARAM].setValue(random::uniform());
    params[Topograph::SN_DENS_PARAM].setValue(random::uniform());
    params[Topograph::HH_DENS_PARAM].setValue(random::uniform());
    barCache.needsRegeneration = true;
}

// NEW: Check if bar needs regeneration based on parameter changes
bool Topograph::checkBarRegenerationNeeded() {
    // Get current parameter values (as uint8_t to match cache storage)
    uint8_t currentMapX = static_cast<uint8_t>(mapX * 255.0f);
    uint8_t currentMapY = static_cast<uint8_t>(mapY * 255.0f);
    uint8_t currentRandomness = static_cast<uint8_t>(chaos * 255.0f);
    uint8_t currentBDDensity = static_cast<uint8_t>(BDFill * 255.0f);
    uint8_t currentSNDensity = static_cast<uint8_t>(SNFill * 255.0f);
    uint8_t currentHHDensity = static_cast<uint8_t>(HHFill * 255.0f);

    // Check if any parameters changed
    bool changed = (
        currentMapX != barCache.lastMapX ||
        currentMapY != barCache.lastMapY ||
        currentRandomness != barCache.lastRandomness ||
        currentBDDensity != barCache.lastDensity[0] ||
        currentSNDensity != barCache.lastDensity[1] ||
        currentHHDensity != barCache.lastDensity[2] ||
        sequencerMode != static_cast<int>(barCache.lastPatternMode) ||
        (accOutputMode == ACC_CLK_RST) != barCache.lastAccAlt
    );

    // For Euclidean mode, also check euclidean lengths
    if (sequencerMode == EUCLIDEAN) {
        uint8_t currentEucX = static_cast<uint8_t>(mapX * 255.0f);
        uint8_t currentEucY = static_cast<uint8_t>(mapY * 255.0f);
        uint8_t currentEucChaos = static_cast<uint8_t>(chaos * 255.0f);

        changed = changed || (
            currentEucX != barCache.lastEuclideanLength[0] ||
            currentEucY != barCache.lastEuclideanLength[1] ||
            currentEucChaos != barCache.lastEuclideanLength[2]
        );
    }

    return changed || barCache.needsRegeneration;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////// Widget //////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Redesign panel for phasor-based operation

TopographWidget::TopographWidget(Topograph *module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/TopographPanel.svg")));

    // Screws
    addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // Text labels
    std::string fontPath = "res/din1451alt.ttf";

    PlainText* mapXText = new PlainText;
    mapXText->box.pos = Vec(27.1, 208.5);
    mapXText->size = 14;
    mapXText->fontPath = fontPath;
    mapXText->color = nvgRGB(0xFF, 0xFF, 0xFF);
    mapXText->text = "Map X";
    addChild(mapXText);

    PlainText* mapYText = new PlainText;
    mapYText->box.pos = Vec(27.1, 268.5);
    mapYText->size = 14;
    mapYText->fontPath = fontPath;
    mapYText->color = nvgRGB(0xFF, 0xFF, 0xFF);
    mapYText->text = "Map Y";
    addChild(mapYText);

    PlainText* chaosText = new PlainText;
    chaosText->box.pos = Vec(27.1, 329);
    chaosText->size = 14;
    chaosText->fontPath = fontPath;
    chaosText->color = nvgRGB(0xFF, 0xFF, 0xFF);
    chaosText->text = "Chaos";
    addChild(chaosText);

    // Parameters (matching original panel layout)
    addParam(createParam<Rogan1PSWhite>(Vec(49, 166.15), module, Topograph::MAPX_PARAM));
    addParam(createParam<Rogan1PSWhite>(Vec(49, 226.15), module, Topograph::MAPY_PARAM));
    addParam(createParam<Rogan1PSWhite>(Vec(49, 286.15), module, Topograph::CHAOS_PARAM));
    addParam(createParam<Rogan1PSBrightRed>(Vec(121, 40.15), module, Topograph::BD_DENS_PARAM));
    addParam(createParam<Rogan1PSOrange>(Vec(157, 103.15), module, Topograph::SN_DENS_PARAM));
    addParam(createParam<Rogan1PSYellow>(Vec(193, 166.15), module, Topograph::HH_DENS_PARAM));

    // Inputs
    addInput(createInput<PJ301MDarkSmall>(Vec(17.0, 50.0), module, Topograph::PHASOR_INPUT));
    addInput(createInput<PJ301MDarkSmall>(Vec(17.0, 176.0), module, Topograph::MAPX_CV));
    addInput(createInput<PJ301MDarkSmall>(Vec(17.0, 236.0), module, Topograph::MAPY_CV));
    addInput(createInput<PJ301MDarkSmall>(Vec(17.0, 296.0), module, Topograph::CHAOS_CV));
    addInput(createInput<PJ301MDarkSmall>(Vec(131.0, 236.0), module, Topograph::BD_FILL_CV));
    addInput(createInput<PJ301MDarkSmall>(Vec(167.0, 236.0), module, Topograph::SN_FILL_CV));
    addInput(createInput<PJ301MDarkSmall>(Vec(203.0, 236.0), module, Topograph::HH_FILL_CV));

    // Outputs
    addOutput(createOutput<PJ301MDarkSmallOut>(Vec(131.0, 296.0), module, Topograph::BD_OUTPUT));
    addOutput(createOutput<PJ301MDarkSmallOut>(Vec(167.0, 296.0), module, Topograph::SN_OUTPUT));
    addOutput(createOutput<PJ301MDarkSmallOut>(Vec(203.0, 296.0), module, Topograph::HH_OUTPUT));
    addOutput(createOutput<PJ301MDarkSmallOut>(Vec(131.0, 331.0), module, Topograph::BD_ACC_OUTPUT));
    addOutput(createOutput<PJ301MDarkSmallOut>(Vec(167.0, 331.0), module, Topograph::SN_ACC_OUTPUT));
    addOutput(createOutput<PJ301MDarkSmallOut>(Vec(203.0, 331.0), module, Topograph::HH_ACC_OUTPUT));

    // Lights
    addChild(createLight<MediumLight<RedLight>>(Vec(151.5, 314.5), module, Topograph::BD_LIGHT));
    addChild(createLight<MediumLight<RedLight>>(Vec(187.5, 314.5), module, Topograph::SN_LIGHT));
    addChild(createLight<MediumLight<RedLight>>(Vec(223.5, 314.5), module, Topograph::HH_LIGHT));
}

void TopographWidget::appendContextMenu(Menu* menu) {
    Topograph* module = dynamic_cast<Topograph*>(this->module);
    if (!module) return;
    
    menu->addChild(new MenuSeparator);
    
    // Sequencer mode
    menu->addChild(createIndexSubmenuItem("Sequencer Mode", {"Henri", "Original", "Euclidean"},
        [=]() { return module->sequencerMode; },
        [=](int mode) { 
            module->sequencerMode = (Topograph::SequencerMode)mode;
            module->patternGenerator.setPatternMode((PatternGeneratorMode)mode);
            module->barCache.needsRegeneration = true;
        }
    ));
    
    // Trigger output mode
    menu->addChild(createIndexSubmenuItem("Trigger Mode", {"Pulse", "Gate"},
        [=]() { return module->triggerOutputMode; },
        [=](int mode) { module->triggerOutputMode = (Topograph::TriggerOutputMode)mode; }
    ));
    
    // Accent output mode
    menu->addChild(createIndexSubmenuItem("Accent Mode", {"Individual", "Clock+Reset"},
        [=]() { return module->accOutputMode; },
        [=](int mode) { 
            module->accOutputMode = (Topograph::AccOutputMode)mode;
            module->patternGenerator.setAccentAltMode(mode == 1);
            module->barCache.needsRegeneration = true;
        }
    ));
    
    // Panel style
    menu->addChild(createIndexSubmenuItem("Panel", {"Dark", "Light"},
        [=]() { return module->panelStyle; },
        [=](int style) { module->panelStyle = style; }
    ));
}

void TopographWidget::step() {
    Topograph* module = dynamic_cast<Topograph*>(this->module);
    if (module) {
        // Panel style switching could go here if needed
    }
    ModuleWidget::step();
}

Model *modelTopograph = createModel<Topograph, TopographWidget>("PhasorTopograph");
