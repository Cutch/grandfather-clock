from time import delay
from gpiozero import OutputDevice as stepper
import boto3
import os
import math
client = boto3.client('sqs')


step_pin = 22
direction_pin = 27
stepper_enable = 23

enable_stepper = stepper(stepper_enable)
step = stepper(step_pin)
direction = stepper(direction_pin)

def oneStep():
    step.on()
    delay(0.002)
    step.off()

def degToStep(deg):
    return math.ceil(360/200*deg)

def rotate(deg):
    for i in degToStep(deg):
        oneStep()

def run():
    enable_stepper.on()
    delay(100)
    rotate(450)
    delay(500)
    rotate(-90)
    delay(500)
    rotate(90)
    delay(500)
    rotate(-90)
    delay(500)
    rotate(90)
    delay(500)
    rotate(-450)
    delay(100)
    enable_stepper.off()


while True:
    resp = client.receive_message(
        QueueUrl='https://sqs.xx-xxxx-x.amazonaws.com/xxxxxxxxxxx/ClockQueue',
        WaitTimeSeconds=60
    )
    print(resp)
    if 'Messages' in resp:
        messages = resp['Messages']
        if len(messages) > 0:
            message = messages[0]['body']
            print('Received Message', message)
            run()
    delay(2)

