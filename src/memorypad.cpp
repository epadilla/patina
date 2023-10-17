#include <deque>

#include "plugin.hpp"

struct MemoryPad : Module {
  enum ParamId {
    TRACKPAD_X_PARAM,
    TRACKPAD_Y_PARAM,
    PATH_DIRECTION_PARAM,
    SPEED_PARAM,
    X_POLARITY_PARAM,
    Y_POLARITY_PARAM,
    X_ATTN_PARAM,
    Y_ATTN_PARAM,
    PARAMS_LEN
  };
  enum InputId {
    INPUTS_LEN
  };
  enum OutputId {
    X_OUTPUT,
    Y_OUTPUT,
    OUTPUTS_LEN
  };
  enum LightId {
    X_POLARITY_LIGHT,
    Y_POLARITY_LIGHT,
    LIGHTS_LEN
  };

  const int PATH_BASE_SAMPLE_RATE = 60;
  const char* RECORDED_X_PATH_KEY = "RECORDED_X_PATH_KEY";
  const char* RECORDED_Y_PATH_KEY = "RECORDED_Y_PATH_KEY";

  bool _isRecording;
  std::vector<Vec> _recordedPath;
  unsigned long _lastPathIndex = 0;
  int64_t _lastPathFrame = 0;
  float _lastSpeedMultiplier = 0;
  bool _currPathDirectionFwd = true;

  MemoryPad() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    configParam(TRACKPAD_X_PARAM, 0.f, 1.f, 0.f, "");
    configParam(TRACKPAD_Y_PARAM, 0.f, 1.f, 0.f, "");
    configSwitch(PATH_DIRECTION_PARAM, 0.0, 2.0, 0.0, "Path Direction", {"Fwd", "InOut", "Rev"});
    configParam(SPEED_PARAM, 0.1f, 5.f, 1.f, "Speed");
    configSwitch(X_POLARITY_PARAM, 0.f, 1.f, 0.f, "X Polarity", {"Unipolar", "Bipolar"});
    configSwitch(Y_POLARITY_PARAM, 0.f, 1.f, 0.f, "Y Polarity", {"Unipolar", "Bipolar"});
    configParam(X_ATTN_PARAM, 0.f, 1.f, 1.f, "X Attenuation");
    configParam(Y_ATTN_PARAM, 0.f, 1.f, 1.f, "Y Attenuation");
    configOutput(X_OUTPUT, "X Output");
    configOutput(Y_OUTPUT, "Y Output");
  }

  void process(const ProcessArgs& args) override {
    lights[X_POLARITY_LIGHT].setBrightness(params[X_POLARITY_PARAM].getValue() > 0.f);
    lights[Y_POLARITY_LIGHT].setBrightness(params[Y_POLARITY_PARAM].getValue() > 0.f);

    if (_isRecording || _recordedPath.empty()) {
      _setXYOutputs(params[TRACKPAD_X_PARAM].getValue(), params[TRACKPAD_Y_PARAM].getValue());
      return;
    }

    float currentTime = args.sampleTime * args.frame;

    float speedMultiplier = params[SPEED_PARAM].getValue();
    int pathFrame = currentTime * PATH_BASE_SAMPLE_RATE * speedMultiplier;

    if (speedMultiplier != _lastSpeedMultiplier) {
      _lastSpeedMultiplier = speedMultiplier;
      _lastPathFrame = 0;
      return;
    }

    if (pathFrame <= _lastPathFrame) {
      return;
    }

    _lastPathFrame = pathFrame;

    float pathDirection = params[PATH_DIRECTION_PARAM].getValue();

    if (pathDirection == 0.f) {  // Fwd
      _currPathDirectionFwd = true;
      _lastPathIndex++;
      if (_lastPathIndex >= _recordedPath.size()) {
        _lastPathIndex = 0;
      }
    } else if (pathDirection == 1.f) {  // InOut
      if (_lastPathIndex == 0) {
        _currPathDirectionFwd = true;
      } else if (_lastPathIndex == _recordedPath.size() - 1) {
        _currPathDirectionFwd = false;
      }

      _lastPathIndex += (_currPathDirectionFwd) ? 1 : -1;

      if (_lastPathIndex < 0) {
        _lastPathIndex = 0;
      } else if (_lastPathIndex >= _recordedPath.size()) {
        _lastPathIndex = _recordedPath.size();
      }
    } else if (pathDirection == 2.f) {  // Rev
      _currPathDirectionFwd = false;
      if (_lastPathIndex == 0) {
        _lastPathIndex = _recordedPath.size();
      }
      _lastPathIndex--;
    }

    Vec values = _recordedPath[_lastPathIndex];
    _setXYOutputs(values.x, values.y);
  }

  void _setXYOutputs(float x, float y) {
    bool xIsBipolar = params[X_POLARITY_PARAM].getValue() > 0.f;
    bool yIsBipolar = params[Y_POLARITY_PARAM].getValue() > 0.f;

    if (xIsBipolar) {
      x -= 0.5;
      x *= 2;
    }

    if (yIsBipolar) {
      y -= 0.5;
      y *= 2;
    }

    float xAttenuation = params[X_ATTN_PARAM].getValue();
    float yAttenuation = params[Y_ATTN_PARAM].getValue();

    outputs[X_OUTPUT].setVoltage(5.f * x * xAttenuation);
    outputs[Y_OUTPUT].setVoltage(5.f * y * yAttenuation);
  }

  // Serialization
  json_t* dataToJson() override {
    json_t* root = json_object();

    json_t* jXArray = json_array();
    json_t* jYArray = json_array();

    for (size_t i = 0; i < _recordedPath.size(); i++) {
      json_array_append_new(jXArray, json_real(_recordedPath[i].x));
      json_array_append_new(jYArray, json_real(_recordedPath[i].y));
    }

    json_object_set_new(root, RECORDED_X_PATH_KEY, jXArray);
    json_object_set_new(root, RECORDED_Y_PATH_KEY, jYArray);

    return root;
  }

  void dataFromJson(json_t* rootJ) override {
    json_t* jXArray = json_object_get(rootJ, RECORDED_X_PATH_KEY);
    json_t* jYArray = json_object_get(rootJ, RECORDED_Y_PATH_KEY);

    size_t xArraySize = json_array_size(jXArray);
    size_t yArraySize = json_array_size(jYArray);

    if (xArraySize > 0 && xArraySize == yArraySize) {
      _recordedPath.clear();

      size_t idx;
      json_t* xVal;
      json_array_foreach(jXArray, idx, xVal) {
        float x = json_number_value(xVal);

        json_t* yVal = json_array_get(jYArray, idx);
        float y = json_number_value(yVal);

        _recordedPath.push_back(Vec(x, y));
      }
    }
  }
};

struct MemoryPadTrackpad : OpaqueWidget {
  const unsigned long TAIL_MAX_SIZE = 50;

  MemoryPad* module = NULL;
  int _xParamId, _yParamId;
  std::deque<Vec> _puckTail;

  ParamQuantity* getXParamQuantity() {
    if (!module) {
      return NULL;
    }

    return module->paramQuantities[_xParamId];
  }

  ParamQuantity* getYParamQuantity() {
    if (!module) {
      return NULL;
    }

    return module->paramQuantities[_yParamId];
  }

  void onDragStart(const DragStartEvent& e) override {
    OpaqueWidget::onDragStart(e);

    if (module) {
      module->_recordedPath.clear();
      module->_isRecording = true;
    }
  }

  void onDragHover(const DragHoverEvent& e) override {
    OpaqueWidget::onDragHover(e);
    if (e.origin == this) {
      Vec mousePos = e.pos;

      float xPosIdx = mousePos.x / box.size.x;
      float yPosIdx = (box.size.y - mousePos.y) / box.size.y;

      xPosIdx = _constrainPositionIdx(xPosIdx);
      yPosIdx = _constrainPositionIdx(yPosIdx);

      ParamQuantity* xParamQ = getXParamQuantity();
      if (xParamQ) {
        getXParamQuantity()->setValue(xPosIdx);
      }

      ParamQuantity* yParamQ = getYParamQuantity();
      if (yParamQ) {
        getYParamQuantity()->setValue(yPosIdx);
      }

      if (module) {
        module->_recordedPath.push_back(Vec(xPosIdx, yPosIdx));
      }
    }
  }

  float _constrainPositionIdx(float idx) {
    idx = (idx < 0.f) ? 0.f : idx;
    idx = (idx > 1.f) ? 1.f : idx;
    return idx;
  }

  void onDragEnd(const DragEndEvent& e) override {
    OpaqueWidget::onDragEnd(e);

    if (module) {
      module->_isRecording = false;
    }
  }

  void draw(const DrawArgs& args) override {
    OpaqueWidget::draw(args);

    float xParamValue = 0.5f;
    float yParamValue = 0.5f;
    if (module) {
      if (module->_isRecording) {
        xParamValue = (getXParamQuantity()) ? getXParamQuantity()->getValue() : 0.5f;
        yParamValue = (getYParamQuantity()) ? getYParamQuantity()->getValue() : 0.5f;
      } else if (module->_recordedPath.size() > module->_lastPathIndex) {
        Vec lastPoint = module->_recordedPath[module->_lastPathIndex];
        xParamValue = lastPoint.x;
        yParamValue = lastPoint.y;
      }
    }

    float xPos = xParamValue * box.size.x;
    float yPos = box.size.y - (yParamValue * box.size.y);
    Vec puckDot = Vec(xPos, yPos);

    // Draw tail
    for (unsigned long i = 0; i < _puckTail.size(); ++i) {
      float invProgress = 1.f - ((float)i / (float)(TAIL_MAX_SIZE - 1));

      Vec pos = _puckTail[i];

      nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
      nvgBeginPath(args.vg);
      nvgCircle(args.vg, pos.x, pos.y, 8.0);
      NVGpaint p = nvgRadialGradient(args.vg, pos.x, pos.y, 0.f, 8.f * invProgress, nvgTransRGBA(SCHEME_YELLOW, (unsigned char)255 * invProgress), nvgTransRGBAf(SCHEME_YELLOW, 0.f));
      nvgFillPaint(args.vg, p);
      nvgFill(args.vg);
    }

    _puckTail.push_front(Vec(puckDot.x, puckDot.y));
    if (_puckTail.size() > TAIL_MAX_SIZE) {
      _puckTail.pop_back();
    }

    // Draw dot
    nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
    nvgBeginPath(args.vg);
    nvgCircle(args.vg, puckDot.x, puckDot.y, 8.0);
    nvgFillColor(args.vg, SCHEME_YELLOW);
    nvgFill(args.vg);
  }
};

struct MemoryPadWidget : ModuleWidget {
  MemoryPadWidget(MemoryPad* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/memorypad.svg")));

    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    addParam(createParamCentered<CKSSThreeHorizontal>(mm2px(Vec(14.0, 12.38)), module, MemoryPad::PATH_DIRECTION_PARAM));

    addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(37.0, 12.0)), module, MemoryPad::SPEED_PARAM));

    addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(14.0, 75.0)), module, MemoryPad::X_POLARITY_PARAM, MemoryPad::X_POLARITY_LIGHT));
    addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(37.0, 75.0)), module, MemoryPad::Y_POLARITY_PARAM, MemoryPad::Y_POLARITY_LIGHT));

    addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(14.0, 89.0)), module, MemoryPad::X_ATTN_PARAM));
    addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(37.0, 89.0)), module, MemoryPad::Y_ATTN_PARAM));

    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(14.08, 107.3)), module, MemoryPad::X_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(37.12, 107.3)), module, MemoryPad::Y_OUTPUT));

    MemoryPadTrackpad* trackpad = new MemoryPadTrackpad();
    trackpad->box.pos = mm2px(Vec(2.48, 22.88));
    trackpad->box.size = mm2px(Vec(46.08, 46.08));
    trackpad->_xParamId = MemoryPad::TRACKPAD_X_PARAM;
    trackpad->_yParamId = MemoryPad::TRACKPAD_Y_PARAM;
    trackpad->module = module;
    addChild(trackpad);
  }
};

Model* modelMemoryPad = createModel<MemoryPad, MemoryPadWidget>("MemoryPad");