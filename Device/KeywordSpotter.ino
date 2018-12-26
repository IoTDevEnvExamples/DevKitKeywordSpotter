#include <Arduino.h>
#include <stdio.h>
#include <math.h>
#include "OledDisplay.h"
#include "AudioClassV2.h"
#include "stm32412g_discovery_audio.h"
#include "RGB_LED.h"
#include <stdint.h>
#include <SystemTickCounter.h>

#define MFCC_WRAPPER_DEFINED
#include "featurizer.h"
#define MODEL_WRAPPER_DEFINED
#include "classifier.h"

// Arduino build can't seem to handle lots of source files so they are all included here to make one big file.
// Even then you will probably get build errors every second build, in which case you need to delete the temporary build folder.
#ifndef BUTTONS_H
#define BUTTONS_H

class ButtonManager
{
  private:
    int lastButtonAState;
    int lastButtonBState;

  public:
    enum ButtonStates
    {
      None = 0,
      ButtonAPressed = 1,
      ButtonBPressed = 2
    };

    void init()
    {
      // initialize the button pin as a input
      pinMode(USER_BUTTON_A, INPUT);
      lastButtonAState = digitalRead(USER_BUTTON_A);

      // initialize the button B pin as a input
      pinMode(USER_BUTTON_B, INPUT);
      lastButtonBState = digitalRead(USER_BUTTON_B);
    }

    int read()
    {          
      int result = ButtonStates::None;
      int buttonAState = digitalRead(USER_BUTTON_A);
      int buttonBState = digitalRead(USER_BUTTON_B);
      
      if (buttonAState == LOW && lastButtonAState == HIGH)
      {
        result |= ButtonStates::ButtonAPressed;
      }
      if (buttonBState == LOW && lastButtonBState == HIGH)
      {
        result |= ButtonStates::ButtonBPressed;
      }
      lastButtonAState = buttonAState;
      lastButtonBState = buttonBState;
      return result;
    }
};

#endif


#ifndef SIMPLE_TIMER_H
#define SIMPLE_TIMER_H

class SimpleTimer {
public:
    SimpleTimer() 
    {
        started_ = false;
    }
    void start() 
    {
        started_ = true;
        start_ = now();
    }
    void stop() 
    {
        started_ = false;
        end_ = now();
    }
    double seconds() 
    {
        auto diff = static_cast<double>(end() - start_);
        return  diff / 1000.0;
    }
    double milliseconds() 
    {
        return static_cast<double>(end() - start_);
    }
    bool started() 
    {
        return started_;
    }
    static void init() {
        SystemTickCounterInit();
    }
private:
    uint64_t now() {
        return SystemTickCounterRead();
    }
    uint64_t end() {
        if (started_) {
            // not stopped yet, so return "elapsed time so far".
            end_ = SystemTickCounterRead();
        }
        return end_;
    }
    uint64_t start_;
    uint64_t end_;
    bool started_;
};
#endif

#include "SystemTickCounter.h"
#ifndef INSTRUCTION_COUNTER_H
#define INSTRUCTION_COUNTER_H

#include "core_cm4.h"

class InstructionCounter
{
private:
    uint32_t _count;
public:
    const uint32_t DWT_CYCLE_COUNTER_ENABLE_BIT = (1UL << 0);

    InstructionCounter() 
    {
    }
    void Enable()
    {
        DWT->CTRL |= DWT_CYCLE_COUNTER_ENABLE_BIT;
    }
    void Disable()
    {
        DWT->CTRL &= ~DWT_CYCLE_COUNTER_ENABLE_BIT;
    }
    void Start()
    {
        DWT->CYCCNT = 0; // reset the count
    }
    void Stop()
    {
        _count = DWT->CYCCNT;
    }
    uint32_t GetCount() 
    {
        return _count;
    }
};

#endif


AudioClass& Audio = AudioClass::getInstance();

enum AppState 
{
  APPSTATE_Init,
  APPSTATE_Error,
  APPSTATE_Recording
};

static AppState appstate;

// These numbers need to match the compiled ELL models.
const int SAMPLE_RATE = 16000;
const int SAMPLE_BIT_DEPTH = 16;
const int FEATURIZER_INPUT_SIZE = 512;
const int FRAME_RATE = 33; // assumes a "shift" of 512 and 512/16000 = 0.032ms per frame.
const int FEATURIZER_OUTPUT_SIZE = 80;
const int CLASSIFIER_OUTPUT_SIZE = 31;
const float THRESHOLD = 0.9;

static int scaled_input_buffer_pos = 0;
static float scaled_input_buffer[FEATURIZER_INPUT_SIZE]; // raw audio converted to float

const int MAX_FEATURE_BUFFERS = 10; // set to buffer up to 1 second of audio in circular buffer
static float featurizer_input_buffers[MAX_FEATURE_BUFFERS][FEATURIZER_INPUT_SIZE]; // input to featurizer
static int featurizer_input_buffer_read = -1; // next read pos
static int featurizer_input_buffer_write = 0; // next write pos
static int dropped_frames = 0;
static float featurizer_output_buffer[FEATURIZER_OUTPUT_SIZE]; // 40 channels
static float classifier_output_buffer[CLASSIFIER_OUTPUT_SIZE]; // 31 classes

static int raw_audio_count = 0;
static char raw_audio_buffer[AUDIO_CHUNK_SIZE];
static int prediction_count = 0;
static int last_prediction = 0;
static int last_confidence = 0; // as a percentage between 0 and 100.

static uint8_t maxGain = 0;
static uint8_t minGain = 0;

static SimpleTimer hint_timer;
static uint hint_index = 1;
static uint hint_delay = 5; // seconds

extern "C" {
    extern int vad_signal;
}
ButtonManager buttons;
InstructionCounter cycleCounter;

int next(int pos){
  pos++;
  if (pos == MAX_FEATURE_BUFFERS){
    pos = 0;
  }
  return pos;
}

static const char* const categories[] = {
    "background_noise",
    "bed",
    "bird",
    "cat",
    "dog",
    "down",
    "eight",
    "five",
    "four",
    "go",
    "happy",
    "house",
    "left",
    "marvin",
    "nine",
    "no",
    "off",
    "on",
    "one",
    "right",
    "seven",
    "sheila",
    "six",
    "stop",
    "three",
    "tree",
    "two",
    "up",
    "wow",
    "yes",
    "zero"
};

uint max_category = 0;
int last_vad = 0;
float min_level = 100;
float max_level = 0;
RGB_LED rgbLed;

void show_signals(int vad, float level)
{
  if (level < min_level){
    min_level = level;
  }
  if (level > max_level){
    max_level = level;
  }
  int red = 0;
  int green = 0;
  int blue = 0;
  if (vad != last_vad)
  {
    Serial.printf("VAD signal changed from %d to %d\r\n", last_vad, vad);
    last_vad = vad;  
  }

  float range = max_level - min_level;
  if (range == 0) range = 1;
  float brightness = 255 * (level - min_level) / range;
  if (vad) {
    red = brightness;
  } else {
    green = brightness + 30;
    if (green > 255)
        green = 255;
  }
  rgbLed.setColor(red, green, blue);

}

void reset_signals()
{
  min_level = 100;
  max_level = 0;
  last_vad = 0;
}

static uint reset_delay = 1; // s\econds
static bool reset_waiting = false;
static SimpleTimer reset_timer;

void delayed_reset(int vad)
{
  if (vad == 0)
  {
    // no voice, so start delay timer.
    if (!reset_waiting) {
      reset_waiting = true;
      reset_timer.start();
    } else {
      double s = reset_timer.seconds();
      if (s > reset_delay){
        model_Reset();
        Serial.print("#");        
        reset_timer.start();
      }
    }
  }
  else 
  {
    // no reset during voice activity
    reset_waiting =false;
    reset_timer.stop();
  }
}


void display_hint(bool reset)
{
  if (reset) {
    hint_timer.start();
  }
  else 
  {
    hint_timer.stop();
    double s = hint_timer.seconds();
    if (s > hint_delay) {
      Screen.clean();
      Screen.print(0, "Try saying...");
      Screen.print(1, categories[hint_index++]);
      if (hint_index == max_category) {
        hint_index = 1;
      } 
      hint_timer.start();
    }
  }
}

void display_gain()
{
  char buffer[20];
  Screen.clean();
  Screen.print(0, "Gain :  ");  
  if (maxGain == 0)
  {
    Screen.print(1, "off");
  }
  else
  {
    sprintf(buffer, "min=%d", (int)minGain);
    Screen.print(1, buffer);
    sprintf(buffer, "max=%d", (int)maxGain);
    Screen.print(2, buffer);
  }
}

void set_gain()
{
  if (maxGain < minGain) {
    maxGain = minGain;
  }

  if (maxGain == 0) {
    Audio.disableLevelControl();
  }
  else {
    Serial.printf("Enabling ALC max gain %d and min gain %d\r\n", maxGain, minGain);
    Audio.enableLevelControl(maxGain, minGain);
    delay(100);
    int value = Audio.readRegister(0x20);
    Serial.printf("Register %x = %x\r\n", 0x20, value);  
  }

  reset_signals();
  model_Reset();
}

void increase_gain(bool inc_min, bool inc_max)
{
  if (inc_min) {
    minGain++;
  }
  if (inc_max) {
    maxGain++;
  }
  if (maxGain > 7) {
    maxGain = 0; // wrap around
    minGain = 0;
  }
  if (minGain > 7) {
    minGain = 0;
  }
  set_gain();
  display_gain();
}

void show_error(const char* msg)
{
  Screen.clean();
  Screen.print(0, msg);
  appstate = APPSTATE_Error;
}

void check_buttons()
{
  auto state = buttons.read();
  if ((state & ButtonManager::ButtonStates::ButtonAPressed) != 0)
  {
    increase_gain(true, false);
  }
  if ((state & ButtonManager::ButtonStates::ButtonBPressed) != 0)
  {
    increase_gain(false, true);
  }
}

bool get_prediction(float* featurizer_input_buffer)
{
  // looks like <chrono> doesn't work, rats...
  SimpleTimer timer;
  timer.start();

  // mfcc transform
  mfcc_Filter(nullptr, featurizer_input_buffer, featurizer_output_buffer);

  // classifier
  model_Predict(nullptr, featurizer_output_buffer, classifier_output_buffer);

  // calculate a sort of energy level from the mfcc output 
  float level = 0;
  for (int i = 0; i < FEATURIZER_OUTPUT_SIZE; i++)
  {
    float x = featurizer_output_buffer[i];
    level += (x*x);
  }
  
  int vad = vad_signal;
  show_signals(vad, level);

  float max = -1;
  int argmax = 0;
  
  // argmax over predictions.
  for (int j = 0; j < CLASSIFIER_OUTPUT_SIZE; j++)
  {
      float v = classifier_output_buffer[j];
      if (v > max) {
        max = v;
        argmax = j;
      }
  }

  timer.stop();
  float elapsed = timer.milliseconds();
  int percent = (int)(max * 100);  
  bool got_prediction = false;
  if (argmax != 0 && max > THRESHOLD) 
  { 
    if (last_prediction != argmax)
    {
      last_confidence = 0;
    }
    if (last_prediction != argmax || percent > last_confidence) {
      
      if (percent > last_confidence)
      {        
        // Serial.printf("Prediction %d is %s (%.2f) on level %.2f, peak voice %.2f, silence %.2f, vad=%d, in %d ms\r\n", prediction_count++, categories[argmax], (float)percent/100, level, peak_voice, silence_level, vad, (int)elapsed);
        if (last_prediction != argmax && vad && argmax < max_category) {
          //Serial.printf("Prediction %d is %s (%.2f) on level %.2f, peak voice %.2f, silence %.2f, vad=%d, in %d ms\r\n", prediction_count++, categories[argmax], (float)percent/100, level, peak_voice, silence_level, vad, (int)elapsed);
          Serial.printf("Prediction %d is %s (%.2f) on level %.2f, vad=%d, in %d ms\r\n", prediction_count++, categories[argmax], (float)percent/100, level, vad, (int)elapsed);
          Screen.clean();
          Screen.print(0, categories[argmax]);
          if (argmax == hint_index)
          {
            hint_index++;
            if (hint_index == max_category) 
            {
              hint_index = 1;
            } 
          }
          Screen.print(2, "next word:");
          Screen.print(3, categories[hint_index]);
          got_prediction = true;
        }
        
        last_prediction = argmax;
        last_confidence = percent;
      }
    }
    if (last_confidence > 0)
    {
      // to smooth the predictions a bit, it doesn't make sense to get two different predictions 30ms apart, so this
      // little count down on the previous confidence level provides this smoothing effect.
      last_confidence -= 1;
    }
  }
  return got_prediction;
}

void audio_callback()
{
  // this is called when Audio class has a buffer full of audio, the buffer is size AUDIO_CHUNK_SIZE (512)  
  Audio.readFromRecordBuffer(raw_audio_buffer, AUDIO_CHUNK_SIZE);
  raw_audio_count++;
  
  char* curReader = &raw_audio_buffer[0];
  char* endReader = &raw_audio_buffer[AUDIO_CHUNK_SIZE];
  while(curReader < endReader)
  {
    if (SAMPLE_BIT_DEPTH == 16)
    {
      // We are getting 512 samples, but with dual channel 16 bit audio this means we are 
      // getting 512/4=128 readings after converting to mono channel floating point values.
      // Our featurizer expects 256 readings, so it will take 2 callbacks to fill the featurizer
      // input buffer.
      int bytesPerSample = 2;
      
      // convert to mono
      int16_t sample = *((int16_t *)curReader);
      curReader += bytesPerSample * 2; // skip right channel (not the best but it works for now)

      scaled_input_buffer[scaled_input_buffer_pos] = (float)sample / 32768.0f;
      scaled_input_buffer_pos++;
      
      if (scaled_input_buffer_pos == FEATURIZER_INPUT_SIZE)
      {
        scaled_input_buffer_pos = 0;
        if (next(featurizer_input_buffer_write) == featurizer_input_buffer_read)
        {
          dropped_frames++; // dropping frame on the floor since classifier is still processing this buffer
        }
        else 
        {
          memcpy(featurizer_input_buffers[featurizer_input_buffer_write], scaled_input_buffer, FEATURIZER_INPUT_SIZE * sizeof(float));                  
          featurizer_input_buffer_write = next(featurizer_input_buffer_write);
        }
      }
    }
  }
}

void stop_recording()
{
  Audio.stop();
  Screen.clean();
  Serial.println("stop recording");
  Screen.print(0, "stopped.");
}

void start_recording()
{
  appstate = APPSTATE_Recording;
 
  // Re-config the audio data format
  Audio.format(SAMPLE_RATE, SAMPLE_BIT_DEPTH);
  delay(100);
  
  Serial.println("listening...");

  // Start to record audio data
  Audio.startRecord(audio_callback);
}

void setup(void)
{
  SimpleTimer::init();
  appstate = APPSTATE_Init;
  
  max_category = sizeof(categories) / sizeof(char*);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
    
  cycleCounter.Enable();

  Serial.println("ELL Audio Demo!");
  Serial.printf("Recognizing %d keywords\n", max_category);
  
  buttons.init();

  int filter_size = mfcc_GetInputSize(0);
  if (filter_size != FEATURIZER_INPUT_SIZE)
  {
    Serial.printf("Featurizer input size %d is not equal to %d\n", filter_size, FEATURIZER_INPUT_SIZE);
    show_error("Featurizer Error");
  }
  else 
  {
    int model_size = model_GetInputSize(0);
    if (model_size != FEATURIZER_OUTPUT_SIZE)
    {
      Serial.printf("Classifier input size %d is not equal to %d\n", model_size, FEATURIZER_OUTPUT_SIZE);
      show_error("Classifier Error");
    }
    else {

      int model_output_size = model_GetOutputSize(0);
      if (model_output_size != CLASSIFIER_OUTPUT_SIZE)
      {
        Serial.printf("Classifier output size %d is not equal to %d\n", model_output_size, CLASSIFIER_OUTPUT_SIZE);
        show_error("Classifier Error");
      }
    }
  }

  if (appstate != APPSTATE_Error)
  {
    // do a warm up.  
    SimpleTimer timer;
    timer.start();

    ::memset(featurizer_input_buffers[0], 0, FEATURIZER_INPUT_SIZE);

    cycleCounter.Start();

    // give it a whirl !!
    mfcc_Filter(nullptr, featurizer_input_buffers[0], featurizer_output_buffer);

    // classifier
    model_Predict(nullptr, featurizer_output_buffer, classifier_output_buffer);

    cycleCounter.Stop();
    uint32_t count = cycleCounter.GetCount();
    Serial.printf("Filter+Predict Cycle Count=%d\n", count);

    timer.stop();
    Serial.printf("Setup predict took %f ms\r\n", timer.milliseconds());

    // check audio gain and print the result.
    uint32_t id = Audio.readRegister(nau88c10_CHIPID_ADDR);
    if (id == NAU88C10_ID) {
      Serial.printf("Found audio device: NAU88C10\r\n");
    } else {
      Serial.printf("Found audio device: 0x%x\r\n", id);
    }

    // a default gain level of 4 seems to work pretty well.
    maxGain = 3;
    set_gain();

    start_recording();
    
    Screen.clean();
    
    Screen.print(0, "Listening...");
    Screen.print(1, "A = min gain");
    Screen.print(2, "B = max gain");
  }
}


void loop(void)
{
  if (appstate != APPSTATE_Error)
  {
    while (1)
    {
      check_buttons();
    
      if (dropped_frames > 0) {
        Serial.printf("%d dropped frames\n", dropped_frames);        
        dropped_frames = 0;
      }
      // process all the buffered input frames
      while(next(featurizer_input_buffer_read) != featurizer_input_buffer_write)
      {
        featurizer_input_buffer_read = next(featurizer_input_buffer_read);
        bool found = get_prediction(featurizer_input_buffers[featurizer_input_buffer_read]);
        display_hint(found);
        check_buttons();
      }
      delay(10);
    }
  }
  delay(100);
}


