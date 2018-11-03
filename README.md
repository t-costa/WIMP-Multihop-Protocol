# WIMP
Multihop routing protocol based on Directed Diffusion for the WIMP project: Arduinos with an ESP8266 module will be the nodes and a Raspberry pi 3B will be the sink. This is a university project. There are bugs and pieces of code written with extreme rush, I will fix some of the biggest problems, but probably in a very long time!

## Complete Project
![Poster](https://github.com/t-costa/WIMP-Multihop-Protocol/blob/master/WIMP_poster.jpg)

## Protocol functioning
Just a brief overview on the basic functioning of the protocol with examples of the exchanged messages.

### Initialization of the network
![Hello](https://github.com/t-costa/WIMP/blob/master/deploy_chart.jpg)


### Periodic check: OK
![OK](https://github.com/t-costa/WIMP/blob/master/checkOK_chart.jpg)


### Periodic check: Dead parent
![DeadParent](https://github.com/t-costa/WIMP/blob/master/checkDeadParent_chart.jpg)


### Periodic check: Change parent
![Change](https://github.com/t-costa/WIMP/blob/master/checkChangeParent_chart.jpg)

## References
[ESP documentation](http://arduino-esp8266.readthedocs.io/en/latest/)

[Library code and examples](https://github.com/esp8266/Arduino/tree/master/doc)

[Flash firmware](https://h3ron.com/post/programmare-lesp8266-ovvero-arduino-con-il-wifi-a-meno-di-2/)
