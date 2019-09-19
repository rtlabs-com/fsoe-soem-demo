# SOEM including Safety over EtherCAT (FSoE) DEMO

IMPORTANT
========
* The FSoE sample is for demonstration purpose only, to implement an FSoE Device one must follow applicable guidelines and specifications. Good start: https://www.ethercat.org/ETG5101.
* Build and run test\linux\fsoe_sample as legacy SOEM samples
* The FSoE stack is provided as a pre-built library for Linux and Windows. For more information or support for other targets please visit: https://rt-labs.com/products/safety-over-ethercat-fsoe/

BUILDING
========

Prerequisites for all platforms
-------------------------------

 * CMake 2.8.0 or later


Windows (Visual Studio)
-----------------------

 * Start a Visual Studio command prompt then:
   * `mkdir build`
   * `cd build`
   * `cmake .. -G "NMake Makefiles"`
   * `nmake`

Linux
--------------

   * `mkdir build`
   * `cd build`
   * `cmake ..`
   * `make`

Documentation
-------------

SOEM See https://openethercatsociety.github.io/doc/soem/

FSoE See https://github.com/rtlabs-com/fsoe-soem-demo/blob/master/fsoe/doc/fsoe_refman.pdf
