## Overview
Simple `.c/.h`-library for communicating with the Wiznet's W5500 Ethernet chip. The main goal is to make an easy-to-use API for controlling general settings and 8 hardware sockets. Official [ioLibrary](https://github.com/Wiznet/ioLibrary_Driver) may looks difficult to use, it lacks of good documentation and broad examples suit so this library will try to improve in these aspects.

Architectural principles:
  - **supporting documentation and examples for every implemented feature** – comment every step and non-official hack;
  - **follow the datasheet** – 60-page paper is not very detailed but keeps pretty nice level on providing a useful information without redundancy;
  - **modularity and scalability** – you can create any number of Wiznet instances and sockets in them (of course, keep in mind HW restrictions).

Currently, the library doesn't support every feature of W5500 chip and use a pretty straight synchronous approach. Also, this repository is actually a port for STM32F4 platform (tested on STM32F429ZI) and you can use it as a reference for your own port (see next chapter).


## Porting
First of all, adapt library's low level to your host' platform-specific stuff.

1. Implement these functions:
  - `_write_spi` – takes pointer to array of bytes (length starts from 1 byte) and transmit it via SPI in blocking mode;
  - `_read_spi` – receives SPI data in blocking mode and put it in a given buffer array. Both read and write functions manage CS assertion by themselves;
  - `_millis` – implement this to ensure a timeouts' work. On ARM, you can use built-in SysTick timer;
  - `wiznet_hw_reset` – only first part – where RST pin is toggled.
2. Add necessary arguments as `Wiznet` structure' fields so functions above can operate independently after initial setup.
3. Define other useful constants, macros etc.

All other functions use these abstraction layer and do not contain any HW routines. Refer to sources of this repo for help.


## Wiznet management
Prepare periphery (i.e. initialize clocking, debug printf(), SPI, GPIOs (CS, RST, INT), interrupt, SysTick timer etc). Then, instantiate `wiznet_t` structure and initialize it with default values:
```C
wiznet_t wiznet;
wiznet = wiznet_t_init();
```
To use interrupts you will need in access to `wiznet` variable so declare this statement in a global scope. Such structure initialization helps to avoid garbage values if user will forget to fill in some members.

Now, let's fill in public fields of our fresh new structure. For example, like this:
```C
wiznet.hspi = &hspi2;
wiznet.RST_CS_Port = GPIOB;
wiznet.RST_Pin = WIZNET_RST_Pin;
wiznet.CS_Pin = WIZNET_MANUAL_CS_Pin;

for (uint8_t i=0; i<6; i++) {
    wiznet.mac_addr[i] = (uint8_t[]){44,45,46,47,48,49}[i];
    if (i < 4) {
        wiznet.ip_addr[i] = (uint8_t[]){192,168,1,100}[i];
        wiznet.ip_gateway_addr[i] = (uint8_t[]){192,168,1,1}[i];
        wiznet.subnet_mask[i] = (uint8_t[]){255,255,255,0}[i];
    }
}
```

Now you can run `wiznet_init()` function to start communication with the IC and internally register this Wiznet (you can manage multiple Wiznets in one program – up to `NUM_OF_WIZNETS`):
```C
if (wiznet_init(&wiznet) == 0) printf("WIZNET INIT OK\n");
```

During this, RST pin is toggled and 2 flags are polled for completing. `wiznet_get_version()` function is used as a last correctness check of Wiznet reset and initialization. It should always return `4` as it stands in datasheet. In practice, Wiznet initialization takes approximately 3 seconds to fully complete. Ethernet LEDs should start blinking if PHY setup was successful.

If you somehow decide to stop working with Wiznet in your program run `wiznet_deinit()` with your `wiznet_t *` pointer as argument.

Now we can step forward to the socket creation.


## Sockets management
As it for Wiznet management Socket' life starts from new struct and its filling. Assuming we have UDP socket:
```C
socket_t socket1 = socket_t_init();
socket1.type = SOCK_TYPE_UDP;
for (uint8_t i=0; i<4; i++) socket1.ip[i] = (uint8_t[]){192,168,1,214}[i];
socket1.port = 1200;
socket(&wiznet, &socket1);
```

We use same pattern for all 3 types of sockets: TCP, UDP and MACRAW. Last line binds given socket to the given Wiznet, opens (and connects, for TCP) it.

Let's create 2 more sockets:
```C
socket_t socket2 = socket_t_init();
socket2.type = SOCK_TYPE_TCP;
for (uint8_t i=0; i<4; i++) socket2.ip[i] = (uint8_t[]){192,168,1,222}[i];
socket2.port = 1300;
socket(&wiznet, &socket2);

socket_t socket3 = socket_t_init();
socket3.type = SOCK_TYPE_MACRAW;
for (uint8_t i=0; i<6; i++) socket3.macraw_dst[i] = (uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}[i];
sock_status_t s3_status = socket(&wiznet, &socket3);
```

`socket` constructor assigns proper HW socket according to the availability and type (e.g., MACRAW can only be opened in Socket0). Currently, all sockets are allocated with default 2kB TX/RX buffers.

Now let's check status (in different ways) and try to send some data over each protocol. Then close sockets:
```C
uint8_t msg_udp[] = "data over UDP";
uint8_t msg_tcp[] = "data over TCP";
uint8_t msg_macraw[] = "data over MACRAW";

if (socket1.status == SOCK_STATUS_UDP) {
    sendto(&socket1, msg_udp, sizeof(msg_udp));
    sock_close(&socket1);
}

if (socket2.status == SOCK_STATUS_ESTABLISHED) {
    sendto(&socket2, msg_tcp, sizeof(msg_tcp));
    sock_discon(&socket2);
}

if (s3_status == SOCK_STATUS_MACRAW)
    sendto(&socket3, msg_macraw, sizeof(msg_macraw));
sock_close(&socket3);
```

For example, in simple Python UDP server will show:
```
$ python3 udp_server.py
Wait for data...
14 b'data over UDP\x00'
```

You can completely remove and unregister socket using `sock_deinit()` function:
```C
sock_deinit(&socket1);
sock_deinit(&socket2);
sock_deinit(&socket3);
```

After such operation you can reuse corresponding HW sockets for another purposes.

`sendto()` function can handle overflows: if size of transmitting data is bigger than free space then message is fragmenting onto 2 parts which sending continuously by recursive call.

In order to receive information, 2 functions are available: `recv()` and `recv_alloc()`. First one takes static buffer and writes data from HW RX buffer into it. So the case when the SW buffer is smaller is possible. `recv_alloc()` takes only pointer and allocates SW array by itself so it never overflows and always will have exact size.


## Examples
