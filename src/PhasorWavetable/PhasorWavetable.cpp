#include "PhasorWavetable.hpp"

struct PhasorWavetableWidget : HCVModuleWidget {
    PhasorWavetableWidget(PhasorWavetable* module) {
        setSkinPath("res/PhasorWavetablePanel.svg");
        initializeWidget(module);

        // Display (wavetable visualization)
        PhasorWTDisplay<PhasorWavetable>* display = createWidget<PhasorWTDisplay<PhasorWavetable>>(mm2px(Vec(0.0, 19.0)));
        display->box.size = mm2px(Vec(35.56, 29.224));
        display->module = module;
        addChild(display);

        // Position controls (knob + CV depth trimpot + CV input) - left column
        createParamComboVertical(11, 162, PhasorWavetable::POS_PARAM, PhasorWavetable::POS_CV_PARAM, PhasorWavetable::POS_INPUT);

        // Mode button (LFO/VCO toggle) - right column
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(27.0, 56.0)), module, PhasorWavetable::MODE_PARAM, PhasorWavetable::MODE_LIGHT));

        // Invert button with light - right column
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(27.0, 75.0)), module, PhasorWavetable::INVERT_PARAM, PhasorWavetable::INVERT_LIGHT));

        // Offset button with light - right column
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(27.0, 90.0)), module, PhasorWavetable::OFFSET_PARAM, PhasorWavetable::OFFSET_LIGHT));

        const float mainJackY = 108.0f;
        
        // Phasor input (bottom left, aligned with "In" label)
        addInput(createInputCentered<ThemedPJ301MPort>(mm2px(Vec(10.0, mainJackY)), module, PhasorWavetable::PHASOR_INPUT));

        // Wave output (bottom right, centered in "Out" rectangle)
        addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(24.0, mainJackY)), module, PhasorWavetable::WAVE_OUTPUT));
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
