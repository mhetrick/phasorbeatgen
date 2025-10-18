//
// PhasorBeatMap.hpp
// Author: Dale Johnson (original Topograph)
// Modified by: HetrickCV (phasor-based version)
// Contact: valley.audio.soft@gmail.com
// Date: 5/12/2017
//
// Phasor Beat Map, a phasor-driven port of "Mutable Instruments Grids" for VCV Rack
// Original author: Emilie Gillet (emilie.o.gillet@gmail.com)
// https://github.com/pichenettes/eurorack/tree/master/grids
// Copyright 2012 Emilie Gillet.
//
// This source code is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#pragma once
#include "../PhasorBeatMapPlugin.hpp"
#include "../gui/ValleyComponents.hpp"
#include "../timers/Oneshot.hpp"
#include "../DSP/Phasors/HCVPhasorAnalyzers.h"
#include "PhasorBeatMapPatternGenerator.hpp"
#include "../HetrickUtilities.hpp"
#include <iomanip> // setprecision
#include <sstream> // stringstream

struct PhasorBeatMap : Module {
   enum ParamIds {
       MAPX_PARAM,
       MAPY_PARAM,
       CHAOS_PARAM,
       BD_DENS_PARAM,
       SN_DENS_PARAM,
       HH_DENS_PARAM,
       MODE_PARAM,
       NUM_PARAMS
   };

   enum InputIds {
       PHASOR_INPUT,
       MAPX_CV,
       MAPY_CV,
       CHAOS_CV,
       BD_FILL_CV,
       SN_FILL_CV,
       HH_FILL_CV,
       MODE_CV,
       // FREEZE_INPUT,  // Reserved for future use
       NUM_INPUTS
   };

   enum OutputIds {
       BD_OUTPUT,
       SN_OUTPUT,
       HH_OUTPUT,
       BD_ACC_OUTPUT,
       SN_ACC_OUTPUT,
       HH_ACC_OUTPUT,
       NUM_OUTPUTS
   };

   enum LightIds {
       BD_LIGHT,
       SN_LIGHT,
       HH_LIGHT,
       NUM_LIGHTS
   };

   // Pattern generation
   PatternGenerator patternGenerator;
   BarCache barCache;

   // Phasor processing
   HCVPhasorStepDetector stepDetector;
   HCVPhasorResetDetector resetDetector;
   int lastStep = -1;
   bool freezeActive = false;  // For future freeze input

   // Pattern parameters
   float mapX = 0.0;
   float mapY = 0.0;
   float chaos = 0.0;
   float BDFill = 0.0;
   float SNFill = 0.0;
   float HHFill = 0.0;

   // LED Triggers
   Oneshot drumLED[3];
   const LightIds drumLEDIds[3] = {BD_LIGHT, SN_LIGHT, HH_LIGHT};

   // Drum Triggers
   Oneshot drumTriggers[6];
   const OutputIds outIDs[6] = {BD_OUTPUT, SN_OUTPUT, HH_OUTPUT,
                                BD_ACC_OUTPUT, SN_ACC_OUTPUT, HH_ACC_OUTPUT};

   enum SequencerMode {
       ORIGINAL,
       HENRI,
       EUCLIDEAN,
       NUM_SEQUENCER_MODES
   };
   SequencerMode sequencerMode = ORIGINAL;
   int inEuclideanMode = 0;

   enum TriggerOutputMode {
       PULSE,
       GATE
   };
   TriggerOutputMode triggerOutputMode = PULSE;

   int panelStyle;
   int textVisible = 1;

   PhasorBeatMap();
   json_t* dataToJson() override;
   void dataFromJson(json_t *rootJ) override;
   void process(const ProcessArgs &args) override;
   void onSampleRateChange() override;
   void onReset(const ResetEvent& e) override;
   void onRandomize(const RandomizeEvent& e) override;
   void updateUI();

   // Phasor-based playback methods
   bool checkBarRegenerationNeeded();
   void triggerStepOutputs(int step);
};

struct PhasorBeatMapWidget : HCVModuleWidget {
    PhasorBeatMapWidget(PhasorBeatMap *module);
    void appendContextMenu(Menu* menu) override;
    void step() override;

    SvgPanel* darkPanel;
    SvgPanel* lightPanel;
    PlainText* mapXText;
    PlainText* mapYText;
    PlainText* chaosText;
    NVGcolor darkPanelTextColour = nvgRGB(0xFF, 0xFF, 0xFF);
    NVGcolor lightPanelTextColour = nvgRGB(0x00, 0x00, 0x00);
    NVGcolor panelTextColours[2] = {darkPanelTextColour, lightPanelTextColour};
};
