# Basic tests to evidence that the RFM69HW is connected properly to the board

For the RFM69HW to be working, it needs
1. power
2. correct wiring - e.g. GPIO  / hardware SPI (fallback to SPI is possible if I got the wiring wrong)
3. Reset working - currently should be connected to ESP32 reset on startup

At a software level, there is the choice of RFM69 library - I favour the RFM69 library by Felix Rusu ( https://github.com/LowPowerLab ) as I used it previously with Atmel examples, but the documentation states that the support of ESP32 is a best effort basis supported by separate developers (so I am not sure what to expect). I could also use the MySensors library (https://github.com/mysensors )- but at first glance I have not been filled with confidence that it will provide a library that is usable (only in that it is there to fulfil a purpose and if that purpose is not what you want then it did not feel as if the documentation was good enough to show you the best way to do things). Anyway, we shall soon see.....

Initially this project will test the Felix Rusu library and see how it works (or not..)

This project is working on the basis that earlier projects have been shown to work - e.g. wiring should be good + TFT screen is operational. This means that we can display lots of debug diagnostics on the screen to assist in establishing whether it all works.


## Project update

The first board I soldered was tested incrementally

1. first that the supporting circuitry worked - e.g power regulators / USB-C pins (these were crazy to solder!) - this was fine
2. add the ESP32 WROOM and check that it starts up with output being fed to RX / TX and that it can be flashed with new software - this worked fine
3. added the TFT screen and checked that worked (this uses the ESP32 hardware SPI of HSPI)  - this worked fine (so hence on to this Project to add the RFM69HW and test it)
4. adding the RFM69HW to the board - at this point the results started to go awry. The TFT screen did not work and neither did the RFM69HW - but the ESP32 was working perfectly fine 

I tried to debug the issue in both software and electronically but it was not obvious which element was causing the issue

1. it wasn't likely to be power related - i.e. if the RFM69HW was shorted or causing the voltage to dip too much then it is unlikely the ESP32 would work - so as the ESP32 was working I did not think this was the issue
2. the SPI pinouts were unconnected - e.g. using VSPI for RFM69HW and HSPI for TFT Screen so I could not see how adding the RFM69 could cause the previously working TFT to stop working (i.e. by having one wire cause a conflict)

I decided to solder up another board, both for practice but also with the aim to incrementally build it up again and see if the problem is replicated + at the same stage having the second board would also be useful if / when it was working, as one needs to have a test using **two** boards to see something transmit and something receive.

The second board worked better than the first
1. the first board did not play well with the software project - i.e. as a test I added a call to radio->readRegs() which should read all the contents of the RFM69 registers over SPI and dump them - but all it dumped were zero's. The second board dumped the following (showing an SPI  connection was seemingly working)

```
Address - HEX - BIN
1 - 4 - 100
2 - 0 - 0
3 - 2 - 10
4 - 40 - 1000000
5 - 3 - 11
6 - 33 - 110011
7 - 6C - 1101100
8 - 40 - 1000000
9 - 0 - 0
A - 41 - 1000001
B - 40 - 1000000
C - 2 - 10
D - 92 - 10010010
E - F5 - 11110101
F - 20 - 100000
10 - 24 - 100100
11 - 7F - 1111111
12 - 9 - 1001
13 - F - 1111
14 - 40 - 1000000
15 - B0 - 10110000
16 - 7B - 1111011
17 - 9B - 10011011
18 - 8 - 1000
19 - 42 - 1000010
1A - 8A - 10001010
1B - 40 - 1000000
1C - 80 - 10000000
1D - 6 - 110
1E - 10 - 10000
```
2. The TFT screen worked, even whilst the RFM69 was connected and operating (as shown above)

3. I tried to test the second board by flashing a transmitter test to an old Moteino board - which should ideally show some activity on my new ESP32 board + software Project, but I could not get any sign that it was transmitting or receiving anything. A simple call on the ESP32 to 

`radio->sendWithRetry(2, "START", 5,0,0);`

testing if the board would transmit did not work as expected as it never returned from this call (at least not in the 5 mins that I waited). 


**at this point (for now) I have decided to go down a different tack and order some new RA-01 Lora boards + update the PCB design to incorporate all the design issues + incorporate some new learning. I realise that I can spend a long time figuring out what is wrong - but in this case all I want is to get a LORA board working with an ESP32 and 'maybe' the better idea is to use a newer component (after all I have had the RFM68HW lying around for >5 years so maybe I should move on)**