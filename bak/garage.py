from time import sleep
from gpiozero.pins.pigpio import PiGPIOFactory
from gpiozero import Device
Device.pin_factory = PiGPIOFactory()
from gpiozero import OutputDevice
from gpiozero import DistanceSensor
import boto3
import os

client = boto3.client('sqs')
s3_client = boto3.client('s3')
relay = OutputDevice(4)
relay.off()

BUCKET_NAME = 'cutch-s3'
update_s3=0
last_status=False
sensor = DistanceSensor(echo=27, trigger=17)

queue_url = 'https://sqs.xx-xxxx-x.amazonaws.com/xxxxxxxxxxx/Garage.fifo'
def isOpen():
    return sensor.distance < 0.5 

def toggleGarage():
    relay.on()
    sleep(1)
    relay.off()
    sleep(10)
    update_s3=0
    
def raiseGarage():
    if not isOpen():
        print('Garage Open')
        toggleGarage()
    else:
        print('Garage Already Opened')
    
def lowerGarage():
    if isOpen():
        print('Garage Close')
        toggleGarage()
    else:
        print('Garage Already Closed')

while True:
    resp = client.receive_message(
        QueueUrl=queue_url,
        WaitTimeSeconds=20
    )
    if 'Messages' in resp:
        messages = resp['Messages']
        if len(messages) > 0:
            message = messages[0]['Body']
            print('Recieved Message', message)
            if message == 'OPEN':
                raiseGarage()
            elif message == 'CLOSE':
                lowerGarage()
            client.delete_message(
                QueueUrl=queue_url,
                ReceiptHandle=messages[0]['ReceiptHandle']
            )
    if isOpen() != last_status:
        update_s3 = 0
    if update_s3 < 3:
        if isOpen():
            print('Garage is Open')
            s3_client.put_object(
                Bucket=BUCKET_NAME,
                Key='_IS_OPEN',
                Body="")
        else:
            print('Garage is Closed')
            s3_client.delete_object(
                Bucket=BUCKET_NAME,
                Key='_IS_OPEN')
        update_s3+=1
    print(sensor.distance)

