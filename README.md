# Eye-BioAmp-Firmware
Firmware for Eye BioAmp hardware from Upside Down Labs


| No. | Program| Description |
| ---- | ------ | ---------- |
|1 | [FixedSampling](1_FixedSampling)| Sample from ADC at a fixed rate for easy processing of signal.|
|2 | [EOGFilter](2_EOGFilter)| A 0.5 - 19.5 Hz band-pass filter for clean Electrooculography.|
|3 | [DrowsinessDetection](3_DrowsinessDetection)| Drowsiness detection using eye blink detection.|
|4 | [EOGPhotoCaptureBLE](4_EOGPhotoCaptureBLE)| EOG based photo clicking machine using ESP32 BLE.|
|5 | [EyeBlinkDetection](5_EyeBlinkDetection)| EOG based eye blink detection.|


## Examples

1. **EOG Filter**

    A band-pass filter for EOG signals between 0.5 Hz and 19.5 Hz

    <img src="2_EOGFilter/EOGFilter.png" height="300" width="400">
    

2. **Eye Blink Detection**

    Eye Blink Detection using EOG signals.

    <img src="2_EOGFilter/EOG-Demo.png" height="300" width="500">