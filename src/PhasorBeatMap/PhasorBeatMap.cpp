//
// PhasorBeatMap.cpp
// Author: Dale Johnson
// Contact: valley.audio.soft@gmail.com
// Date: 5/12/2017
//

#include "PhasorBeatMap.hpp"

PhasorBeatMap::PhasorBeatMap() {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

    // Parameters
    configParam(PhasorBeatMap::MAPX_PARAM, 0.0, 1.0, 0.0, "Pattern Map X");
    configParam(PhasorBeatMap::MAPY_PARAM, 0.0, 1.0, 0.0, "Pattern Map Y");
    configParam(PhasorBeatMap::CHAOS_PARAM, 0.0, 1.0, 0.0, "Pattern Chaos");
    configParam(PhasorBeatMap::BD_DENS_PARAM, 0.0, 1.0, 0.5, "Channel 1 Density");
    configParam(PhasorBeatMap::SN_DENS_PARAM, 0.0, 1.0, 0.5, "Channel 2 Density");
    configParam(PhasorBeatMap::HH_DENS_PARAM, 0.0, 1.0, 0.5, "Channel 3 Density");

    // Inputs
    configInput(PhasorBeatMap::PHASOR_INPUT, "Phasor");
    configInput(PhasorBeatMap::MAPX_CV, "Map X CV");
    configInput(PhasorBeatMap::MAPY_CV, "Map Y CV");
    configInput(PhasorBeatMap::CHAOS_CV, "Chaos CV");
    configInput(PhasorBeatMap::BD_FILL_CV, "Channel 1 Density CV");
    configInput(PhasorBeatMap::SN_FILL_CV, "Channel 2 Density CV");
    configInput(PhasorBeatMap::HH_FILL_CV, "Channel 3 Density CV");

    // Outputs
    configOutput(PhasorBeatMap::BD_OUTPUT, "Channel 1");
    configOutput(PhasorBeatMap::SN_OUTPUT, "Channel 2");
    configOutput(PhasorBeatMap::HH_OUTPUT, "Channel 3");
    configOutput(PhasorBeatMap::BD_ACC_OUTPUT, "Channel 1 Accent");
    configOutput(PhasorBeatMap::SN_ACC_OUTPUT, "Channel 2 Accent");
    configOutput(PhasorBeatMap::HH_ACC_OUTPUT, "Channel 3 Accent");

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

    // Set default pattern mode to Original
    patternGenerator.setPatternMode(PATTERN_ORIGINAL);
}

json_t* PhasorBeatMap::dataToJson() {
    json_t *rootJ = json_object();
    json_object_set_new(rootJ, "sequencerMode", json_integer(sequencerMode));
    json_object_set_new(rootJ, "triggerOutputMode", json_integer(triggerOutputMode));
    json_object_set_new(rootJ, "panelStyle", json_integer(panelStyle));
    return rootJ;
}

void PhasorBeatMap::dataFromJson(json_t* rootJ) {
    json_t *sequencerModeJ = json_object_get(rootJ, "sequencerMode");
    if (sequencerModeJ) {
        sequencerMode = (PhasorBeatMap::SequencerMode) json_integer_value(sequencerModeJ);
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
		triggerOutputMode = (PhasorBeatMap::TriggerOutputMode) json_integer_value(triggerOutputModeJ);
	}

    json_t* panelStyleJ = json_object_get(rootJ, "panelStyle");
    if (panelStyleJ) {
        panelStyle = (int)json_integer_value(panelStyleJ);
    }
}

void PhasorBeatMap::process(const ProcessArgs &args) {
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

    // Update trigger/gate outputs
    if (triggerOutputMode == GATE) {
        // Gate mode: output high for first 50% of step if trigger is active
        int currentStep = stepDetector.getCurrentStep();
        float fractionalStep = stepDetector.getFractionalStep();
        const BarCache::StepData& data = barCache.steps[currentStep];

        for (int i = 0; i < 3; ++i) {
            bool gateHigh = data.trigger[i] && (fractionalStep < 0.5f);
            outputs[outIDs[i]].setVoltage(gateHigh ? 10.0f : 0.0f);

            // Accent outputs
            bool accentGateHigh = data.trigger[i] && data.accent[i] && (fractionalStep < 0.5f);
            outputs[outIDs[i + 3]].setVoltage(accentGateHigh ? 10.0f : 0.0f);
        }
    } else {
        // Pulse mode: use trigger generators
        for (int i = 0; i < 6; ++i) {
            drumTriggers[i].process();
            outputs[outIDs[i]].setVoltage(drumTriggers[i].getState() ? 10.0f : 0.0f);
        }
    }

    // Update UI
    updateUI();
}

void PhasorBeatMap::updateUI() {
    for(int i = 0; i < 3; ++i) {
        drumLED[i].process();
        lights[drumLEDIds[i]].value = drumLED[i].getState() ? 1.0f : 0.0f;
    }
}

// Trigger outputs for a specific step based on cached bar data
void PhasorBeatMap::triggerStepOutputs(int step) {
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

void PhasorBeatMap::onSampleRateChange() {
    for(int i = 0; i < 3; ++i) {
        drumLED[i].setSampleRate(APP->engine->getSampleRate());
    }
    for(int i = 0; i < 6; ++i) {
        drumTriggers[i].setSampleRate(APP->engine->getSampleRate());
    }
}

void PhasorBeatMap::onReset(const ResetEvent& e) {
    Module::onReset(e);
    barCache.needsRegeneration = true;
}

void PhasorBeatMap::onRandomize(const RandomizeEvent& e) {
    params[PhasorBeatMap::MAPX_PARAM].setValue(random::uniform());
    params[PhasorBeatMap::MAPY_PARAM].setValue(random::uniform());
    params[PhasorBeatMap::CHAOS_PARAM].setValue(random::uniform());
    params[PhasorBeatMap::BD_DENS_PARAM].setValue(random::uniform());
    params[PhasorBeatMap::SN_DENS_PARAM].setValue(random::uniform());
    params[PhasorBeatMap::HH_DENS_PARAM].setValue(random::uniform());
    barCache.needsRegeneration = true;
}

// NEW: Check if bar needs regeneration based on parameter changes
bool PhasorBeatMap::checkBarRegenerationNeeded() {
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
        sequencerMode != static_cast<int>(barCache.lastPatternMode)
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

// Sequencer mode menu item
struct SeqModeItem : MenuItem {
    PhasorBeatMap* module;
    int sequencerModeChoice;

    void onAction(const event::Action& e) override {
        module->sequencerModeChoice = sequencerModeChoice;

        // Map choice index to SequencerMode enum
        switch (sequencerModeChoice) {
            case 0: // Original
                module->sequencerMode = PhasorBeatMap::ORIGINAL;
                module->patternGenerator.setPatternMode(PATTERN_ORIGINAL);
                module->inEuclideanMode = 0;
                break;
            case 1: // Henri
                module->sequencerMode = PhasorBeatMap::HENRI;
                module->patternGenerator.setPatternMode(PATTERN_HENRI);
                module->inEuclideanMode = 0;
                break;
            case 2: // Euclidean
                module->sequencerMode = PhasorBeatMap::EUCLIDEAN;
                module->patternGenerator.setPatternMode(PATTERN_EUCLIDEAN);
                module->inEuclideanMode = 1;
                break;
        }
        module->barCache.needsRegeneration = true;
    }
};

// Sequencer mode choice menu
struct SeqModeChoice : ValleyChoiceMenu {
    PhasorBeatMap* module;
    std::vector<std::string> seqModeLabels = {"Original", "Henri", "Euclid"};

    void onAction(const event::Action& e) override {
        if (!module) {
            return;
        }

        ui::Menu* menu = createMenu();
        for (int i = 0; i < static_cast<int>(seqModeLabels.size()); ++i) {
            SeqModeItem* item = new SeqModeItem;
            item->module = module;
            item->sequencerModeChoice = i;
            item->text = seqModeLabels[i];
            item->rightText = CHECKMARK(item->sequencerModeChoice == module->sequencerModeChoice);
            menu->addChild(item);
        }
    }

    void step() override {
        text = module ? seqModeLabels[module->sequencerModeChoice] : seqModeLabels[0];
    }
};

PhasorBeatMapWidget::PhasorBeatMapWidget(PhasorBeatMap *module) 
{
    setSkinPath("res/PhasorBeatMap.svg");
    initializeWidget(module);


    // Parameters (matching original panel layout)
    addParam(createParam<Rogan1PSWhite>(Vec(49, 166.15), module, PhasorBeatMap::MAPX_PARAM));
    addParam(createParam<Rogan1PSWhite>(Vec(49, 226.15), module, PhasorBeatMap::MAPY_PARAM));
    addParam(createParam<Rogan1PSWhite>(Vec(49, 286.15), module, PhasorBeatMap::CHAOS_PARAM));
    addParam(createParam<Rogan1PSBrightRed>(Vec(121, 40.15), module, PhasorBeatMap::BD_DENS_PARAM));
    addParam(createParam<Rogan1PSOrange>(Vec(157, 103.15), module, PhasorBeatMap::SN_DENS_PARAM));
    addParam(createParam<Rogan1PSYellow>(Vec(193, 166.15), module, PhasorBeatMap::HH_DENS_PARAM));

    // Inputs
    createInputPort(17.0, 50.0, PhasorBeatMap::PHASOR_INPUT);

    // Pattern mode dropdown below phasor input
    SeqModeChoice* modeChoice = new SeqModeChoice;
    modeChoice->module = module;
    modeChoice->box.pos = Vec(25.0, 120.0);
    modeChoice->box.size.x = 55.f;
    addChild(modeChoice);

    createInputPort(17.0, 176.0, PhasorBeatMap::MAPX_CV);
    createInputPort(17.0, 236.0, PhasorBeatMap::MAPY_CV);
    createInputPort(17.0, 296.0, PhasorBeatMap::CHAOS_CV);
    createInputPort(131.0, 236.0, PhasorBeatMap::BD_FILL_CV);
    createInputPort(167.0, 236.0, PhasorBeatMap::SN_FILL_CV);
    createInputPort(203.0, 236.0, PhasorBeatMap::HH_FILL_CV);

    // Outputs
    createOutputPort(131.0, 276.0, PhasorBeatMap::BD_OUTPUT);
    createOutputPort(167.0, 276.0, PhasorBeatMap::SN_OUTPUT);
    createOutputPort(203.0, 276.0, PhasorBeatMap::HH_OUTPUT);
    createOutputPort(131.0, 311.0, PhasorBeatMap::BD_ACC_OUTPUT);
    createOutputPort(167.0, 311.0, PhasorBeatMap::SN_ACC_OUTPUT);
    createOutputPort(203.0, 311.0, PhasorBeatMap::HH_ACC_OUTPUT);

    // Lights
    createHCVRedLight(138.6, 218, PhasorBeatMap::BD_LIGHT);
    createHCVRedLight(174.6, 218, PhasorBeatMap::SN_LIGHT);
    createHCVRedLight(210.6, 218, PhasorBeatMap::HH_LIGHT);
}

void PhasorBeatMapWidget::appendContextMenu(Menu* menu) {
    PhasorBeatMap* module = dynamic_cast<PhasorBeatMap*>(this->module);
    if (!module) return;

    menu->addChild(new MenuSeparator);

    // Trigger output mode
    menu->addChild(createIndexSubmenuItem("Trigger Mode", {"Pulse", "Gate"},
        [=]() { return module->triggerOutputMode; },
        [=](int mode) { module->triggerOutputMode = (PhasorBeatMap::TriggerOutputMode)mode; }
    ));
}

void PhasorBeatMapWidget::step() {
    ModuleWidget::step();
}

Model *modelPhasorBeatMap = createModel<PhasorBeatMap, PhasorBeatMapWidget>("PhasorBeatMap");
