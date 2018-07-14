#ifndef WIZNET_H_
#define WIZNET_H_



#include <printf_redirection.h>

#include "spi.h"
#include "gpio.h"

#include <stdlib.h>
#include <stdbool.h>



/*
 *  This macro allows us to declare and use 2-bytes-variables in their natural
 *  form and read/write in correct Wiznet byte-ordering
 *
 *    ex.1:
 *          uint16_t port = 1200;
 *          printf("Port: %d\n", port);
 *          port = SWAP_TWO_BYTES(port);
 *          _write_spi( ... , (uint8_t *)&port, sizeof(uint16_t) );
 *
 *    ex.2:
 *          uint16_t port;
 *          _read_spi( ... , (uint8_t *)&port, sizeof(uint16_t) );
 *          port = SWAP_TWO_BYTES(port);
 *          printf("Port: %d\n", port);
 *
 */
#define SWAP_TWO_BYTES(X) ((((X)&(0xFF00))>>8)|(((X)&(0x00FF))<<8))


// Number of Wiznets
#define NUM_OF_WIZNETS 1

// Number of sockets available. You should also adjust sock_n_* arrays
#define NUM_OF_SOCKETS 8


// Read/Write Bit of Control Phase
#define RWB 2


/*
 *  BSB[4:0] bits of Control Phase
 */
#define COMMON_REGISTERS 0b00000

// Mode Register and its bits
#define MR 0x0000  // 1 byte
//#define MR_RST 7  // SW reset ('1' for reset, wait until '0')
// #define WOL 5  // Wake on LAN
// #define PB 4  // Ping Block ('0' - disable ping block)
// #define PPPoE 3  // set this to '1' for ADSL
// #define FARP 1  // set this to '1' to force sending ARP requests

#define GAR 0x0001  // Gateway IP Address Register (4 bytes)
#define SUBR 0x0005  // Subnet Mask Register (4 bytes)
#define SHAR 0x0009  // Source MAC-address Register (6 bytes)
#define SIPR 0x000F  // Source IP Address Register (4 bytes)

/*
 *  Interrupt Assert Wait Time, i.e. pause after clearing first interrupt, and
 *  second was triggered when first hasn't been handled completely. After Iawt
 *  time, INTn pin will fire again so you can handle this second interrupt too
 */
#define INTLEVEL 0x0013  // 2 bytes

// #define IR 0x0015  // Interrupt Register (1 byte)
// #define IMR 0x0016  // Interrupt Mask Register (1 byte)
#define SIR 0x0017  // Socket Interrupt Register (1 byte)
#define SIMR 0x0018  // Socket Interrupt Mask Register (1 byte)

// PHY Configuration Register and its bits
#define PHYCFGR 0x002E  // 1 byte
#define PHYCFGR_RST 7  // check this bit to know when reset is completed
#define LNK 0  // check this bit to know whether PHY link is up

#define VERSIONR 0x0039  // HW version (always equals to '4')


/*
 *  BSB[4:0] bits of Control Phase
 */
#define SOCKET_0_REGISTERS 0b00001
#define SOCKET_0_TX_BUFFER 0b00010
#define SOCKET_0_RX_BUFFER 0b00011

#define SOCKET_1_REGISTERS 0b00101
#define SOCKET_1_TX_BUFFER 0b00110
#define SOCKET_1_RX_BUFFER 0b00111

#define SOCKET_2_REGISTERS 0b01001
#define SOCKET_2_TX_BUFFER 0b01010
#define SOCKET_2_RX_BUFFER 0b01011

#define SOCKET_3_REGISTERS 0b01101
#define SOCKET_3_TX_BUFFER 0b01110
#define SOCKET_3_RX_BUFFER 0b01111

#define SOCKET_4_REGISTERS 0b10001
#define SOCKET_4_TX_BUFFER 0b10010
#define SOCKET_4_RX_BUFFER 0b10011

#define SOCKET_5_REGISTERS 0b10101
#define SOCKET_5_TX_BUFFER 0b10110
#define SOCKET_5_RX_BUFFER 0b10111

#define SOCKET_6_REGISTERS 0b11001
#define SOCKET_6_TX_BUFFER 0b11010
#define SOCKET_6_RX_BUFFER 0b11011

#define SOCKET_7_REGISTERS 0b11101
#define SOCKET_7_TX_BUFFER 0b11110
#define SOCKET_7_RX_BUFFER 0b11111

// Mode Register and its bits
#define Sn_MR 0x0000  // 1 byte
// #define MULTI_MFEN 7
// #define BCASTB 6
// #define ND_MC_MMB 5
// #define UCASTB_MIP6B 4
#define SOCK_TYPE_CLOSED 0b0000
typedef enum SockType {
    SOCK_TYPE_TCP=0b0001,
    SOCK_TYPE_UDP=0b0010,
    SOCK_TYPE_MACRAW=0b0100
} sock_type_t;

// Socket Command Register
#define Sn_CR 0x0001  // 1 byte
typedef enum SockCmd {
    SOCK_CMD_OPEN=0x01,
    SOCK_CMD_LISTEN=0x02,
    SOCK_CMD_CONNECT=0x04,
    SOCK_CMD_DISCON=0x08,
    SOCK_CMD_CLOSE=0x10,
    SOCK_CMD_SEND=0x20,
    SOCK_CMD_SEND_MAC=0x21,
    SOCK_CMD_SEND_KEEP=0x22,
    SOCK_CMD_RECV=0x40
} sock_cmd_t;

// Socket Interrupt Register
#define Sn_IR 0x0002  // 1 byte
typedef enum SockISRType {
    SOCK_IR_CON,
    SOCK_IR_DISCON,
    SOCK_IR_RECV,
    SOCK_IR_TIMEOUT,
    SOCK_IR_SEND_OK,

    // not wiznet value, just for our needs
    NUM_OF_SOCK_IRS
} sock_isr_type_t;

// Socket Interrupt Mask Register (1 byte)
// #define Sn_IMR 0x002C  // default to 0xFF - i.e all interrupts enabled

// Socket Status Register
#define Sn_SR 0x0003  // 1 byte
typedef enum SockStatus {
    SOCK_STATUS_CLOSED=0x00,
    SOCK_STATUS_INIT=0x13,
    SOCK_STATUS_LISTEN=0x14,
    SOCK_STATUS_ESTABLISHED=0x17,
    SOCK_STATUS_CLOSE_WAIT=0x1C,
    SOCK_STATUS_UDP=0x22,
    SOCK_STATUS_MACRAW=0x42,

    // not wiznet values, just for our needs
    SOCK_STATUS_NUM_EXCEEDED=-2,
    SOCK_STATUS_MACRAW_TAKEN=-3,
    SOCK_STATUS_CANT_OPEN=-4,
    SOCK_STATUS_CANT_CLOSE=-5
} sock_status_t;

#define Sn_PORT 0x0004  // incoming port (2 bytes)
#define Sn_DHAR 0x0006  // destination MAC address (bypass ARP) (6 bytes)
#define Sn_DIPR 0x000C  // destination IP address (4 bytes)
#define Sn_DPORT 0x0010  // destination port (2 bytes)

#define Sn_MSSR 0x0012  // Maximum Segment Size (2 bytes)

#define Sn_TX_FSR 0x0020  // TX buffer Free Size Register (2 bytes)
#define Sn_TX_RD 0x0022  // TX buffer start pointer (2 bytes)
#define Sn_TX_WR 0x0024  // TX buffer end pointer (2 bytes)

#define Sn_RX_RSR 0x0026  // RX buffer Received Size Register (2 bytes)
#define Sn_RX_RD 0x0028  // RX buffer start pointer (2 bytes)
#define Sn_RX_WR 0x002A  // RX buffer end pointer (2 bytes)



typedef struct Socket socket_t;
typedef struct Wiznet wiznet_t;

/*
 *  Struct representing socket of any type - UDP, TCP or MACRAW (pure Ethernet)
 */
struct Socket {
    // private members
    int8_t _id;  // ID is assigned by socket() function after successful creation.
                 // ID is equal to Wiznet's HW sockets 0-7
    wiznet_t *_host_wiznet;  // pointer to the Wiznet structure that hosted
                             // this socket

    // public members
    uint8_t type;
    sock_status_t status;
    uint8_t ip[4];
    uint16_t port;
    uint8_t macraw_dst[6];
};

/*
 *  Struct representing single Wiznet
 */
struct Wiznet {
    // private members
    int8_t _id;
    uint8_t _sockets_cnt;
    uint8_t _sockets_taken;  // mask like 0b01010101 where LSB is Socket0 and
                             // MSB is Socket7
    socket_t *_sockets[NUM_OF_SOCKETS];  // array of pointers to Sockets 0-7
                                         // (corresponds with _sockets_taken, i.e.
                                         // _sockets[0] is Socket0 and so on)

    // platform-specific definitions
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *RST_CS_Port;
    uint16_t RST_Pin;
    uint16_t CS_Pin;

    // public members
    uint8_t mac_addr[6];
    uint8_t ip_addr[4];
    uint8_t ip_gateway_addr[4];
    uint8_t subnet_mask[4];
};



/*
 *  Public functions - Wiznet-related
 */
wiznet_t wiznet_t_init(void);
int32_t wiznet_init(wiznet_t *wiznet);
void wiznet_deinit(wiznet_t *wiznet);

void wiznet_hw_reset(wiznet_t *wiznet);

uint8_t wiznet_get_version(wiznet_t *wiznet);

void wiznet_isr_handler(wiznet_t *wiznet);


/*
 *  Public functions - sockets-related
 */
socket_t socket_t_init(void);
sock_status_t socket(wiznet_t *wiznet, socket_t *sock);
void sock_reset(socket_t *sock);
void sock_deinit(socket_t *sock);

void sock_open(socket_t *sock);
void sock_connect(socket_t *sock);

void sendto(socket_t *sock, uint8_t *data, uint16_t len);
uint16_t recv(socket_t *sock, uint8_t *buf, uint16_t buf_size);
uint16_t recv_alloc(socket_t *sock, uint8_t **buf);

void sock_discon(socket_t *sock);
void sock_close(socket_t *sock);



#endif /* WIZNET_H_ */
