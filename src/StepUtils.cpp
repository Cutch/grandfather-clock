#include "StepUtils.h"

Stepper::Stepper(){

}
int Stepper::getStep(){
  return step;
}
void Stepper::setStep(int _step){
  step = _step;
}

void Stepper::oneStep(int pinSpeed) {
  #if !FAKE_SPIN
    digitalWrite(DIRECTION_PIN, direction ? HIGH : LOW);
    digitalWrite(STEP_PIN, HIGH);
    if (direction) {
      step++;
    } else {
      step--;
    }
    delayMicroseconds(pinSpeed);
    digitalWrite(STEP_PIN, LOW);
  #endif
  delayMicroseconds(pinSpeed);
}

int Stepper::degToStep(int deg) {
  return (int)ceil((double)TOOTH_COUNT / 360.0 * (double)deg);
}

void Stepper::rotateByStep(int stepCount) {
  rotateByStep(stepCount, PIN_SPEED);
}

void Stepper::rotateByStep(int stepCount, int pinSpeed) {
  direction = stepCount >= 0 ? 1 : 0;
  if (stepCount < 0) stepCount = abs(stepCount);
  for (int i = 0; i < stepCount; i++) {
    oneStep(pinSpeed);
  }
}

void Stepper::rotate(int deg) {
  rotateByStep(degToStep(deg));
}

bool Stepper::resetToZeroStep() {
  if (step != 0) {
    rotateByStep(-step);
    return true;
  }
  return false;
}

void Stepper::forceMove(int degrees) {
  rotate(degrees);
  step = 0;
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
    delayMicroseconds(PIN_SPEED);
  }
}
void Stepper::disable(){
  if(enabled){
    enabled = false;
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIRECTION_PIN, LOW);
    digitalWrite(STEPPER_ENABLED_PIN, HIGH);
    delayMicroseconds(PIN_SPEED);
  }
}