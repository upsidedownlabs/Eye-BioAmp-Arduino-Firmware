# Eye-BioAmp-Firmware
Firmware for Eye BioAmp hardware from Upside Down Labs


| No. | Program| Description |
| ---- | ------ | ---------- |
|1 | [FixedSampling](1_FixedSampling)| Sample from ADC at a fixed rate for easy processing of signal.|
|2 | [EOGFilter](2_EOGFilter)| A 0.5 - 19.5 Hz band-pass filter for clean Electrooculography.|
|3 | [DrowsinessDetection](3_DrowsinessDetection)| Drowsiness detection using eye blink detection.|
|4 | [EOGPhotoCaptureBLE](4_EOGPhotoCaptureBLE)| EOG based photo clicking machine using ESP32 BLE.|
|5 | [EyeBlinkDetection](5_EyeBlinkDetection)| EOG based eye blink detection.|
|6 | [EOGDinoGame](6_EOGDinoGame)| Controlling chrome dino game using eye blinks.|
|7 | [EOGServoControl](7_EOGServoControl)| Controlling 3D printed servo claw using eye blinks.|

Compatibility of various boards with Brain-BioAmp sensors
<table>
    <thead>
        <tr>
            <th>No.</th>
            <th>Development Board</th>
             <th>Maximum ADC Resolution</th>
            <th>Sensor</th>
            <th>Compatibility</th>
        </tr>
    </thead>
    <tbody>
        <tr>
            <td >1</td>
            <td align= "center" >Arduino UNO R3<br>&<br>Maker UNO</td>
            <td align = "center" >10 bits</td>
            <td>Bioamp EXG Pill</td>
            <td align = "center">✅</td>
        </tr>
        <tr>
            <td >2</td>
            <td align = "center" >Arduino UNO R4 Minima<br>&<br>Arduino UNO R4 Wifi</td>
            <td align = "center">14 bits</td>
            <td>Bioamp EXG Pill</td>
            <td align= "center">✅</td>
        </tr>
        <tr>
            <td >3</td>
            <td align = "center" >Raspberry Pi Pico</td>
            <td align = "center" >12 bits</td>
            <td>Bioamp EXG Pill</td>
            <td align = "center">❌</td>
        </tr>
       <tr>
             <td >4</td>
            <td align= "center"  >ESP32 C3 MINI</td>
           <td align = "center" >12 bits</td>
            <td align = "center">Bioamp EXG Pill</td>
            <td align= "center">❌</td>
        </tr>
        <tr>
             <td >5</td>
            <td align= "center"  >Arduino Due</td>
           <td align = "center" >12 bits</td>
            <td>Bioamp EXG Pill</td>
            <td align = "center">✅</td>
        </tr>
        <tr>
             <td>6</td>
            <td align= "center"  >Arduino Nano</td>
           <td align = "center" >10 bits</td>
            <td>Bioamp EXG Pill</td>
            <td align = "center">✅</td>
        </tr>
        </tbody>
    </table>


## Examples

1. **EOG Filter**

    A band-pass filter for EOG signals between 0.5 Hz and 19.5 Hz

    <img src="2_EOGFilter/EOGFilter.png" height="300" width="400">
    

2. **Eye Blink Detection**

    Eye Blink Detection using EOG signals.

    <img src="2_EOGFilter/EOG-Demo.png" height="300" width="500">
