#include "StepUtils.h"

Stepper::Stepper(){

}
int Stepper::getStep(){
  return step;
}
void Stepper::setStep(int _step){
  step = _step;
}

void Stepper::oneStep() {
  #if !FAKE_SPIN
    digitalWrite(DIRECTION_PIN, direction ? HIGH : LOW);
    digitalWrite(STEP_PIN, HIGH);
    if (direction) {
      step++;
    } else {
      step--;
    }
    delay(PIN_SPEED);
    digitalWrite(STEP_PIN, LOW);
  #endif
  delay(PIN_SPEED);
}

int Stepper::degToStep(int deg) {
  return (int)ceil((double)TOOTH_COUNT / 360.0 * (double)deg);
}


void Stepper::rotateByStep(int stepCount) {
  enable();
  direction = stepCount >= 0 ? 1 : 0;
  if (stepCount < 0) stepCount = abs(stepCount);
  for (int i = 0; i < stepCount; i++) {
    oneStep();
  }
  disable();
}

void Stepper::rotate(int deg) {
  rotateByStep(degToStep(deg));
}

bool Stepper::resetToZeroStep() {
  if (step != 0) {
    enable();
    rotateByStep(-step);
    disable();
    return true;
  }
  return false;
}

void Stepper::forceMove(int degrees) {
  enable();
  rotate(degrees);
  step = 0;
  disable();
}

void Stepper::initialize(){
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIRECTION_PIN, OUTPUT);
  pinMode(STEPPER_ENABLED_PIN, OUTPUT);
  enabled = true;
  disable();
}
void Stepper::enable(){
  if(!enabled){
    enabled = true;
    digitalWrite(STEPPER_ENABLED_PIN, LOW);
    delay(PIN_SPEED);
  }
}
void Stepper::disable(){
  if(enabled){
    enabled = false;
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIRECTION_PIN, LOW);
    digitalWrite(STEPPER_ENABLED_PIN, HIGH);
    delay(PIN_SPEED);
  }
}