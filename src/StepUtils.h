#ifndef StepUtils_h
#define StepUtils_h
#include <Arduino.h>

#define STEP_PIN 4
#define DIRECTION_PIN 0
#define STEPPER_ENABLED_PIN 15
#define PIN_SPEED 2
#define TOOTH_COUNT 200
#define FAKE_SPIN false

class Stepper {
    public:
        Stepper(); 
        void initialize();
        void enable();
        void disable();
        void rotate(int deg);
        bool resetToZeroStep();
        void rotateByStep(int stepCount);
        void rotateByStep(int stepCount, int pinSpeed);
        int getStep();
        void setStep(int _step);
        void forceMove(int degrees);
        int degToStep(int deg);
    protected:
        int direction;
        int step; 
        int enabled; 
        void oneStep(int pinSpeed);
};
#endif
