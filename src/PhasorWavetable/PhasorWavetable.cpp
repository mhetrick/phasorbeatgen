#include "PhasorWavetable.hpp"

struct PhasorWavetableWidget : HCVModuleWidget {
    PhasorWavetableWidget(PhasorWavetable* module) {
        setSkinPath("res/PhasorWavetablePanel.svg");
        initializeWidget(module);

        // Display (wavetable visualization)
        PhasorWTDisplay<PhasorWavetable>* display = createWidget<PhasorWTDisplay<PhasorWavetable>>(mm2px(Vec(0.004, 13.04)));
        display->box.size = mm2px(Vec(35.56, 29.224));
        display->module = module;
        addChild(display);

        // Phase light (below display)
        addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(17.731, 49.409)), module, PhasorWavetable::PHASE_LIGHT));

        // Mode button (LFO/VCO toggle) - left side where freq knob would be
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(8.913, 56.388)), module, PhasorWavetable::MODE_PARAM, PhasorWavetable::MODE_LIGHT));

        // Position knob (large, right side)
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(26.647, 56.388)), module, PhasorWavetable::POS_PARAM));

        // Position CV attenuverter (small trimpot)
        addParam(createParamCentered<Trimpot>(mm2px(Vec(28.662, 80.536)), module, PhasorWavetable::POS_CV_PARAM));

        // Invert button with light
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(17.824, 80.517)), module, PhasorWavetable::INVERT_PARAM, PhasorWavetable::INVERT_LIGHT));

        // Offset button with light
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(17.824, 96.859)), module, PhasorWavetable::OFFSET_PARAM, PhasorWavetable::OFFSET_LIGHT));

        // Position CV input
        addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(28.662, 96.859)), module, PhasorWavetable::POS_INPUT));

        // Phasor input (bottom left)
        addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(6.987, 113.115)), module, PhasorWavetable::PHASOR_INPUT));

        // Wave output (bottom right)
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(28.662, 113.115)), module, PhasorWavetable::WAVE_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        PhasorWavetable* module = dynamic_cast<PhasorWavetable*>(this->module);
        assert(module);

        menu->addChild(new MenuSeparator);

        // Mode selection
        menu->addChild(createIndexPtrSubmenuItem("Mode", {"LFO (no antialiasing)", "VCO (antialiased)"},
            &module->params[PhasorWavetable::MODE_PARAM].value
        ));

        menu->addChild(new MenuSeparator);

        // Wavetable menu items
        module->wavetable.appendContextMenu(menu);
    }
};


Model* modelPhasorWavetable = createModel<PhasorWavetable, PhasorWavetableWidget>("PhasorWavetable");
