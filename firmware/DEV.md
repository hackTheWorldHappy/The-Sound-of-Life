because of bad network you need to increase network timeout:
C:\Users\<USER  >\.arduinoIDE\arduino-cli.yaml

add this:
network:
  connection_timeout: 300s


---

using Arduino and this board manager:

https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json


install libs
