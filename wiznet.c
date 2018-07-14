// TODO: picture of RX/TX processes
// TODO: check socket status when calling sendto() and recv()
// TODO: update socket status after sending and receiving
// TODO: use built-in WIZNET timeout feature instead of _millis() (need interrupts)
// TODO: implement wiznet_sw_reset() and wiznet_phy_reset() functions

#include "wiznet.h"


/*
 *  Other global settings and definitions
 */

// timeout for SPI transmitting/receiving (in milliseconds)
#define WIZNET_SPI_TX_TIMEOUT 0xFFFF
#define WIZNET_SPI_RX_TIMEOUT 0xFFFF

// Wiznet' Interrupt Assert Waiting Time
#define IAWT 31249  // 31249 - 5ms @ 25MHz

#define MAX_TCP_SEGMENT_SIZE 1460

// different timeouts (in milliseconds)
#define WIZNET_TIMEOUT_RESET 8000
#define SOCK_TIMEOUT_OPEN 1000
#define SOCK_TIMEOUT_CONNECT 2000
#define SOCK_TIMEOUT_CLOSE 1000
#define SOCK_TIMEOUT_DISCON 2000


/*
 *  BSB[4:0] bits of Control Phase
 */
const uint8_t sock_n_registers[NUM_OF_SOCKETS] = {
    SOCKET_0_REGISTERS,
    SOCKET_1_REGISTERS,
    SOCKET_2_REGISTERS,
    SOCKET_3_REGISTERS,
    SOCKET_4_REGISTERS,
    SOCKET_5_REGISTERS,
    SOCKET_6_REGISTERS,
    SOCKET_7_REGISTERS
};
const uint8_t sock_n_tx_buffers[NUM_OF_SOCKETS] = {
    SOCKET_0_TX_BUFFER,
    SOCKET_1_TX_BUFFER,
    SOCKET_2_TX_BUFFER,
    SOCKET_3_TX_BUFFER,
    SOCKET_4_TX_BUFFER,
    SOCKET_5_TX_BUFFER,
    SOCKET_6_TX_BUFFER,
    SOCKET_7_TX_BUFFER
};
const uint8_t sock_n_rx_buffers[NUM_OF_SOCKETS] = {
    SOCKET_0_RX_BUFFER,
    SOCKET_1_RX_BUFFER,
    SOCKET_2_RX_BUFFER,
    SOCKET_3_RX_BUFFER,
    SOCKET_4_RX_BUFFER,
    SOCKET_5_RX_BUFFER,
    SOCKET_6_RX_BUFFER,
    SOCKET_7_RX_BUFFER
};



/*
 *  For multiple Wiznets management
 */
uint32_t wiznets_cnt = 0;
wiznet_t *wiznets[NUM_OF_WIZNETS];



/*
 *  Implement this to use timeouts when polling something
 */
static uint32_t _millis(void) {
    return HAL_GetTick();
}


/*
 *  Private low-level routine to write 'len' bytes of 'data' buffer to corresponding 'wiznet', 'bank'
 *  and 'addr'
 */
static void _write_spi(wiznet_t *wiznet, uint16_t addr, uint8_t bank, uint8_t *data, uint16_t len) {

    // BSB[4:0] bits
    uint8_t ctrl_phase = bank << 3;
    // 'Write' flag
    ctrl_phase |= 1<<RWB;

    addr = SWAP_TWO_BYTES(addr);

    // CS select
    HAL_GPIO_WritePin(wiznet->RST_CS_Port, wiznet->CS_Pin, GPIO_PIN_RESET);

    // Address Phase, Control Phase and Data Phase
    HAL_SPI_Transmit(wiznet->hspi, (uint8_t *)&addr, sizeof(uint16_t), WIZNET_SPI_TX_TIMEOUT);
    HAL_SPI_Transmit(wiznet->hspi, &ctrl_phase, sizeof(uint8_t), WIZNET_SPI_TX_TIMEOUT);
    HAL_SPI_Transmit(wiznet->hspi, data, len, WIZNET_SPI_TX_TIMEOUT);

    // CS deselect
    HAL_GPIO_WritePin(wiznet->RST_CS_Port, wiznet->CS_Pin, GPIO_PIN_SET);
}


/*
 *  Private low-level routine to read 'len' bytes to 'buf' buffer of corresponding 'wiznet', 'bank'
 *  and 'addr'
 */
static void _read_spi(wiznet_t *wiznet, uint16_t addr, uint8_t bank, uint8_t *buf, uint16_t len) {

    // BSB[4:0] bits
    uint8_t ctrl_phase = bank << 3;
    // 'Read' flag - '0' means read operation so we simply don't set it
    // ctrl_phase &= ~(1<<RWB);

    addr = SWAP_TWO_BYTES(addr);

    // CS select
    HAL_GPIO_WritePin(wiznet->RST_CS_Port, wiznet->CS_Pin, GPIO_PIN_RESET);

    // Address Phase, Control Phase and Data Phase
    HAL_SPI_Transmit(wiznet->hspi, (uint8_t *)&addr, sizeof(uint16_t), WIZNET_SPI_TX_TIMEOUT);
    HAL_SPI_Transmit(wiznet->hspi, &ctrl_phase, sizeof(uint8_t), WIZNET_SPI_TX_TIMEOUT);
    HAL_SPI_Receive(wiznet->hspi, buf, len, WIZNET_SPI_RX_TIMEOUT);

    // CS deselect
    HAL_GPIO_WritePin(wiznet->RST_CS_Port, wiznet->CS_Pin, GPIO_PIN_SET);
}



/*
 *  Initialize 'Wiznet' structure with default values. Always call this function before
 *  any other operations with Wiznet to prevent undefined behavior
 *
 *    ex.: wiznet_t my_wiznet = wiznet_t_init()
 *
 */
wiznet_t wiznet_t_init(void) {

    wiznet_t wiznet = {
        // fill private members
        ._id = -1,
        ._sockets_cnt = 0,
        ._sockets_taken = 0b00000000,
        ._sockets = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},

        // fill in public members in case user will forget to define them
        .mac_addr = {0,0,0,0,0,0},
        .ip_addr = {0,0,0,0},
        .ip_gateway_addr = {0,0,0,0},
        .subnet_mask = {0,0,0,0}
    };

    return wiznet;
}


/*
 *  Initialize new Wiznet. Before calling this function, fill in all necessary fields of
 *  the 'wiznet' structure. Returns '0' at success and non-zero value otherwise
 */
int32_t wiznet_init(wiznet_t *wiznet) {

    // initialize Wiznets array
    static bool is_first_wiznet = true;
    if (is_first_wiznet) {
        is_first_wiznet = false;
        for (uint8_t i=0; i<NUM_OF_WIZNETS; i++) wiznets[i] = NULL;
    }

    // add this Wiznet to the array of Wiznets
    if (++wiznets_cnt > NUM_OF_WIZNETS) {
        printf("TOO MANY WIZNETS\n");
        wiznets_cnt--;
        return -1;
    }
    else {
        for (uint32_t i=0; i<NUM_OF_WIZNETS; i++) {
            if (wiznets[i] == NULL) {
                wiznets[i] = wiznet;
                wiznet->_id = i;
            }
        }
    }

    wiznet_hw_reset(wiznet);

    // set Interrupt Assert Waiting Time
    uint16_t i_awt = IAWT;
    i_awt = SWAP_TWO_BYTES(i_awt);
    _write_spi(wiznet, INTLEVEL, COMMON_REGISTERS, (uint8_t *)&i_awt, sizeof(uint16_t));

    // set MAC address
    _write_spi(wiznet, SHAR, COMMON_REGISTERS, wiznet->mac_addr, 6);
    // set this Wiznet' IP address
    _write_spi(wiznet, SIPR, COMMON_REGISTERS, wiznet->ip_addr, 4);
    // set gateway' IP address
    _write_spi(wiznet, GAR, COMMON_REGISTERS, wiznet->ip_gateway_addr, 4);
    // set subnet mask
    _write_spi(wiznet, SUBR, COMMON_REGISTERS, wiznet->subnet_mask, 4);

    // DEBUG START
    uint8_t mac_addr_2[6];
    uint8_t ip_addr_2[4];
    uint8_t ip_gateway_addr_2[4];
    uint8_t subnet_mask_2[4];
    _read_spi(wiznet, SHAR, COMMON_REGISTERS, mac_addr_2, 6);
    _read_spi(wiznet, SIPR, COMMON_REGISTERS, ip_addr_2, 4);
    _read_spi(wiznet, GAR, COMMON_REGISTERS, ip_gateway_addr_2, 4);
    _read_spi(wiznet, SUBR, COMMON_REGISTERS, subnet_mask_2, 4);
    printf("WIZNET MAC-address: 0x%X.0x%X.0x%X.0x%X.0x%X.0x%X\n",
            mac_addr_2[0],
            mac_addr_2[1],
            mac_addr_2[2],
            mac_addr_2[3],
            mac_addr_2[4],
            mac_addr_2[5]);
    printf("WIZNET IP-address: %d.%d.%d.%d\n",
            ip_addr_2[0],
            ip_addr_2[1],
            ip_addr_2[2],
            ip_addr_2[3]);
    printf("WIZNET IPgateway-address: %d.%d.%d.%d\n",
            ip_gateway_addr_2[0],
            ip_gateway_addr_2[1],
            ip_gateway_addr_2[2],
            ip_gateway_addr_2[3]);
    printf("WIZNET subnet mask: %d.%d.%d.%d\n",
            subnet_mask_2[0],
            subnet_mask_2[1],
            subnet_mask_2[2],
            subnet_mask_2[3]);
    printf("WIZNET version: %d\n", wiznet_get_version(wiznet));
    // DEBUG END

    uint8_t version = wiznet_get_version(wiznet);
    return (version == 4) ? 0 : -1;
}


/*
 *  Unregister Wiznet 'wiznet'
 */
void wiznet_deinit(wiznet_t *wiznet) {
    wiznet_hw_reset(wiznet);

	wiznet->_id = -1;
    if (wiznets_cnt) wiznets_cnt--;
    wiznets[wiznet->_id] = NULL;
}


/*
 *  Reset Wiznet using hardware pin
 */
void wiznet_hw_reset(wiznet_t *wiznet) {
    HAL_GPIO_WritePin(wiznet->RST_CS_Port, wiznet->RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(1);  // 500 us - from datasheet
    HAL_GPIO_WritePin(wiznet->RST_CS_Port, wiznet->RST_Pin, GPIO_PIN_SET);
    HAL_Delay(1);  // 1 ms - from datasheet

    // wait for reset completing and PHY link up
    uint8_t byte;
    uint32_t timeout_start = _millis();
    while (1) {
        // read status
        _read_spi(wiznet, PHYCFGR, COMMON_REGISTERS, &byte, sizeof(uint8_t));
        // OK
        if ((byte & (1<<PHYCFGR_RST)) && (byte & (1<<LNK))) {
            printf("WIZNET RESET OK\n");
            break;
        }
        // handle timeout
        if ((_millis()-timeout_start) == WIZNET_TIMEOUT_RESET) {
            printf("WIZNET RESET ERROR\n");
            break;
        }
    }
}


/*
 *  Get HW version of Wiznet chip from VERSIONR register. According to the datasheet, version
 *  is always should be read as 0x04
 */
uint8_t wiznet_get_version(wiznet_t *wiznet) {
    uint8_t version = 0;
    _read_spi(wiznet, VERSIONR, COMMON_REGISTERS, &version, sizeof(uint8_t));
    return version;
}


/*
 *  Single universal handler to manage all types of interrupts of given 'wiznet'. Connect
 *  INTn pin and call this function every falling edge of INTn signal. It automatically
 *  defines interrupt type and clears interrupt in the end
 *
 *  NOTE: currently only Sockets 0-7 interrupts are supported
 */
void wiznet_isr_handler(wiznet_t *wiznet) {

    // read SIR register to find out what Socket trigger an interrupt
    uint8_t sock_int_reg;
    _read_spi(wiznet, SIR, COMMON_REGISTERS, &sock_int_reg, sizeof(uint8_t));
//  if (!sock_int_reg) return;
//  printf("SIR: %d\n", sock_int_reg);

    // get socket with interrupt
    socket_t *sock;
    for (uint8_t idx=0; idx<NUM_OF_SOCKETS; idx++) {
        if ((1<<idx) & sock_int_reg) {
            sock = wiznet->_sockets[idx];
            break;
        }
    }

    uint8_t sock_n_register = sock_n_registers[sock->_id];

    // identify interrupt type
    uint8_t ir_type = 0;
    _read_spi(wiznet, Sn_IR, sock_n_register, &ir_type, sizeof(uint8_t));
//  printf("Sn_IR: %d\n", ir_type);
//  if (!ir_type) return;

    for (uint8_t type=0; type<NUM_OF_SOCK_IRS; type++) {
        if ((1<<type) & ir_type) {
            // insert your code here
            switch (type) {
            case SOCK_IR_CON:
                printf("ISR: CONNECTED\n");
                break;
            case SOCK_IR_DISCON:
                printf("ISR: DISCONNECTED\n");
                break;
            case SOCK_IR_RECV:
                printf("ISR: RECEIVED\n");
                break;
            case SOCK_IR_TIMEOUT:
                printf("ISR: TIMEOUT\n");
                break;
            case SOCK_IR_SEND_OK:
                printf("ISR: SEND OK\n");
                break;
            }
        }
    }

    // clear interrupt
    _write_spi(wiznet, Sn_IR, sock_n_register, &ir_type, sizeof(uint8_t));
    uint8_t byte = 0;
    _write_spi(wiznet, SIR, COMMON_REGISTERS, &byte, sizeof(uint8_t));
}



/*
 *  Initialize 'Socket' structure with default values. Always call this function before
 *  any other operations with Socket to prevent undefined behavior
 *
 *    ex.: socket_t my_sock = socket_t_init();
 *
 */
socket_t socket_t_init(void) {

    socket_t sock = {
        // private members will be initialized in case of successful socket creation
        ._id = -1,
        ._host_wiznet = NULL,

        // fill in public members in case user will forget to define them
        .type = SOCK_TYPE_CLOSED,
        .status = SOCK_STATUS_CLOSED,
        .ip = {0,0,0,0},
        .port = 0,
        .macraw_dst = {0,0,0,0,0,0}
    };

    return sock;
}


/*
 *  Initialize the new socket 'sock' of the 'wiznet' chip. Function automatically finds free
 *  Socket in Wiznet and assigns ID number depends on requested type of socket. It also
 *  trying to open (and connect, in case of TCP) socket. Before calling this function, fill in
 *  all necessary fields of the 'sock' structure
 *
 *  NOTE: currently TCP supports only client role
 */
sock_status_t socket(wiznet_t *wiznet, socket_t *sock) {

    // if there are no more free sockets, exit immediately
    if (wiznet->_sockets_cnt >= NUM_OF_SOCKETS) {
        printf("Number of sockets for this WIZNET has been exceeded\n");
        sock->status = SOCK_STATUS_NUM_EXCEEDED;
        return sock->status;
    }

    // for MACRAW socket, only Socket0 is suitable
    if (sock->type == SOCK_TYPE_MACRAW) {
        if (((1<<0) & wiznet->_sockets_taken) == 0) {
            sock->_id = 0;
        }
        else {
            printf("Socket0 for MACRAW already occupied\n");
            sock->status = SOCK_STATUS_MACRAW_TAKEN;
            return sock->status;
        }
    }
    // if all 7 sockets are taken and we need another one then occupy #0
    else if (wiznet->_sockets_taken == 0b11111110) {
        sock->_id = 0;
    }
    // start to create sockets from #1 and save #0 for MACRAW
    else {
        for (uint8_t i=1; i<NUM_OF_SOCKETS; i++)
            if (((1<<i) & wiznet->_sockets_taken) == 0) {
                sock->_id = i;
                break;
            }
    }
    printf("id: %d, type: %d\n", sock->_id, sock->type);

    // choose appropriate register
    uint8_t sock_n_register = sock_n_registers[sock->_id];
    // assign host Wiznet for opening (and connection) socket. We deassign it back in case of error
    sock->_host_wiznet = wiznet;

    // set mode
    uint8_t byte;
    switch (sock->type) {
    case SOCK_TYPE_UDP:
        byte = SOCK_TYPE_UDP;
        break;
    case SOCK_TYPE_TCP:
        byte = SOCK_TYPE_TCP;
        // set maximum segment size
         uint16_t max_sgmnt_size = MAX_TCP_SEGMENT_SIZE;
         max_sgmnt_size = SWAP_TWO_BYTES(max_sgmnt_size);
         _write_spi(wiznet, Sn_MSSR, sock_n_register, (uint8_t *)&max_sgmnt_size, sizeof(uint16_t));
        break;
    case SOCK_TYPE_MACRAW:
        byte = SOCK_TYPE_MACRAW;
        // set MAC address of destination
        _write_spi(wiznet, Sn_DHAR, sock_n_register, sock->macraw_dst, 6);
        break;
    }
    // byte |= 1<<MULTI_MFEN;  // enable multicasting in UDP mode
    _write_spi(wiznet, Sn_MR, sock_n_register, &byte, sizeof(uint8_t));


    if (sock->type != SOCK_TYPE_MACRAW) {
        // set the same port for Source and Destination
        uint16_t port = SWAP_TWO_BYTES(sock->port);
        _write_spi(wiznet, Sn_PORT, sock_n_register, (uint8_t *)&port, sizeof(uint16_t));
        _write_spi(wiznet, Sn_DPORT, sock_n_register, (uint8_t *)&port, sizeof(uint16_t));
        // set destination IP
        _write_spi(wiznet, Sn_DIPR, sock_n_register, sock->ip, 4);
    }


    // open[ and connect] socket
    sock_open(sock);
    if (sock->type == SOCK_TYPE_TCP) sock_connect(sock);


    // successful opening[ and connection]
    if (sock->status > 0) {
        // add socket to Wiznet
        wiznet->_sockets_cnt++;
        wiznet->_sockets_taken |= (1 << sock->_id);
        wiznet->_sockets[sock->_id] = sock;

        // enable interrupt for this socket
//      _read_spi(wiznet, SIMR, COMMON_REGISTERS, &byte, sizeof(uint8_t));
//      byte |= 1<<sock->_id;
//      _write_spi(wiznet, SIMR, COMMON_REGISTERS, &byte, sizeof(uint8_t));
    }
    // error during opening[ or connection] - undo all changes
    else {
        sock->_id = -1;
        sock->_host_wiznet = NULL;
    }

    // DEBUG START
    uint8_t ip_b[4];
    uint16_t port_b;
    _read_spi(wiznet, Sn_DIPR, sock_n_register, ip_b, 4);
    _read_spi(wiznet, Sn_DPORT, sock_n_register, (uint8_t *)&port_b, sizeof(uint16_t));
    port_b = SWAP_TWO_BYTES(port_b);
    printf("socket status: 0x%X\n", sock->status);
    printf("ip: %d.%d.%d.%d\n", ip_b[0], ip_b[1], ip_b[2], ip_b[3]);
    printf("port: %d\n", port_b);
    // DEBUG END

    return sock->status;
}


/*
 *  Reset (almost) all meaningful registers of socket 'sock'. Other registers either aren't
 *  important or will be overwritten at next initialization. You should call this function only
 *  after closing the socket
 */
void sock_reset(socket_t *sock) {

    // choose appropriate register
    uint8_t sock_n_register = sock_n_registers[sock->_id];

    uint8_t byte = 0;
    uint8_t two_bytes[2] = {0,0};
    uint8_t four_bytes[4] = {0,0,0,0};
    uint8_t six_bytes[6] = {0,0,0,0,0,0};

    // Mode Register
    _write_spi(sock->_host_wiznet, Sn_MR, sock_n_register, &byte, sizeof(uint8_t));
    // Source Port
    _write_spi(sock->_host_wiznet, Sn_PORT, sock_n_register, two_bytes, sizeof(two_bytes));
    // Destination Port
    _write_spi(sock->_host_wiznet, Sn_DPORT, sock_n_register, two_bytes, sizeof(two_bytes));
    // Maximum Segment Size
    _write_spi(sock->_host_wiznet, Sn_MSSR, sock_n_register, two_bytes, sizeof(two_bytes));
    // MAC address of destination
    _write_spi(sock->_host_wiznet, Sn_DHAR, sock_n_register, six_bytes, sizeof(six_bytes));
    // IP address of destination
    _write_spi(sock->_host_wiznet, Sn_DIPR, sock_n_register, four_bytes, sizeof(four_bytes));
}


/*
 *  Function resets socket 'sock' and remove its registration in the host Wiznet. It frees
 *  corresponding HW SocketN for reusing later
 */
void sock_deinit(socket_t *sock) {

    sock_reset(sock);

    // disable interrupt for this socket
    uint8_t byte;
    _read_spi(sock->_host_wiznet, SIMR, COMMON_REGISTERS, &byte, sizeof(uint8_t));
    byte &= ~(1<<sock->_id);
    _write_spi(sock->_host_wiznet, SIMR, COMMON_REGISTERS, &byte, sizeof(uint8_t));

    if (sock->_host_wiznet->_sockets_cnt) sock->_host_wiznet->_sockets_cnt--;
    sock->_host_wiznet->_sockets_taken &= ~(1<<sock->_id);
    sock->_host_wiznet->_sockets[sock->_id] = NULL;
}



/*
 *  Send 'OPEN' command to socket 'sock' and wait for its completion
 */
void sock_open(socket_t *sock) {

    // choose appropriate register
    uint8_t sock_n_register = sock_n_registers[sock->_id];

    // send command
    uint8_t byte = SOCK_CMD_OPEN;
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    // wait for socket opening
    uint8_t status;
    uint32_t timeout_start = _millis();
    while (1) {
        // read status
        _read_spi(sock->_host_wiznet, Sn_SR, sock_n_register, &status, sizeof(uint8_t));
        // different cases
        if ( ((sock->type==SOCK_TYPE_UDP) && (status==SOCK_STATUS_UDP)) ||
             ((sock->type==SOCK_TYPE_TCP) && (status==SOCK_STATUS_INIT)) ||
             ((sock->type==SOCK_TYPE_MACRAW) && (status==SOCK_STATUS_MACRAW)) ) {
            sock->status = status;
            break;
        }
        // handle timeout
        if ((_millis()-timeout_start) == SOCK_TIMEOUT_OPEN) {
            sock->status = SOCK_STATUS_CANT_OPEN;
            break;
        }
    }
}


/*
 *  Send 'CONNECT' command to TCP socket 'sock' and wait for its completion (client mode)
 */
void sock_connect(socket_t *sock) {

    // choose appropriate register
    uint8_t sock_n_register = sock_n_registers[sock->_id];

    // send command
    uint8_t byte = SOCK_CMD_CONNECT;
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    // wait for socket connection
    uint8_t status;
    uint32_t timeout_start = _millis();
    while (1) {
        // read status
        _read_spi(sock->_host_wiznet, Sn_SR, sock_n_register, &status, sizeof(uint8_t));
        // socket successfully connected to the remote server
        if (status == SOCK_STATUS_ESTABLISHED) {
            sock->status = SOCK_STATUS_ESTABLISHED;
            break;
        }
        // handle timeout
        if ((_millis()-timeout_start) == SOCK_TIMEOUT_CONNECT) {
            sock->status = status;
            break;
        }
    }
}



/*
 *  Send data 'data' length of 'len' to socket 'sock'. Function automatically manages of start
 *  and end pointers. If the data is bigger than amount of space in HW TX buffer, function
 *  divides it into 2 parts and transmit them sequentionally (using recursive call)
 */
void sendto(socket_t *sock, uint8_t *data, uint16_t len) {

    // DEBUG START
//  printf("send from socket #%d\n", sock->_id);
    // DEBUG END

    // choose appropriate socket register and TX buffer
    uint8_t sock_n_register = sock_n_registers[sock->_id];
    uint16_t sock_n_tx_buffer = sock_n_tx_buffers[sock->_id];

    // 0. check free size
    static bool need_to_fragment = false;
    uint8_t *ptr_to_next_fragment;
    uint16_t len_of_next_fragment;

    uint16_t tx_buf_free_size;
    _read_spi(sock->_host_wiznet, Sn_TX_FSR, sock_n_register, (uint8_t *)&tx_buf_free_size, sizeof(uint16_t));
    tx_buf_free_size = SWAP_TWO_BYTES(tx_buf_free_size);

    // fragment the data
    if (tx_buf_free_size < len) {
        need_to_fragment = true;
        ptr_to_next_fragment = data+tx_buf_free_size;
        len_of_next_fragment = len-tx_buf_free_size;
        len = tx_buf_free_size;
    }
    else need_to_fragment = false;

    // 1. read the pointer of TX buffer where we need to put a data for transmitting
    uint16_t tx_start_ptr;
    _read_spi(sock->_host_wiznet, Sn_TX_RD, sock_n_register, (uint8_t *)&tx_start_ptr, sizeof(uint16_t));
    tx_start_ptr = SWAP_TWO_BYTES(tx_start_ptr);

    // 2. write a data in TX buffer
    _write_spi(sock->_host_wiznet, tx_start_ptr, sock_n_tx_buffer, data, len);

    // 3. set the pointer to the end of a data to be transmitted
    uint16_t tx_end_ptr = len+tx_start_ptr;
    tx_end_ptr = SWAP_TWO_BYTES(tx_end_ptr);
    _write_spi(sock->_host_wiznet, Sn_TX_WR, sock_n_register, (uint8_t *)&tx_end_ptr, sizeof(uint16_t));

    // 4. flush
    uint8_t byte;
    switch (sock->type) {
    case SOCK_TYPE_TCP:
    case SOCK_TYPE_UDP:
        byte = SOCK_CMD_SEND;
        break;
    case SOCK_TYPE_MACRAW:
        byte = SOCK_CMD_SEND_MAC;
        break;
    }
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    if (need_to_fragment) sendto(sock, ptr_to_next_fragment, len_of_next_fragment);
}


/*
 *  Read data from HW RX buffer of socket 'sock' into array 'buf' with size of 'buf_size'. Function
 *  determines and returns number of bytes have been read
 */
uint16_t recv(socket_t *sock, uint8_t *buf, uint16_t buf_size) {
    // TODO: case when we do not read incoming data for a long time

    // choose appropriate socket register and RX buffer
    uint8_t sock_n_register = sock_n_registers[sock->_id];
    uint16_t sock_n_rx_buffer = sock_n_rx_buffers[sock->_id];

    // 1. read start and end pointers of RX buffer with our data
    uint16_t rx_start_ptr, rx_end_ptr;
    _read_spi(sock->_host_wiznet, Sn_RX_RD, sock_n_register, (uint8_t *)&rx_start_ptr, sizeof(uint16_t));
    _read_spi(sock->_host_wiznet, Sn_RX_WR, sock_n_register, (uint8_t *)&rx_end_ptr, sizeof(uint16_t));
    rx_start_ptr = SWAP_TWO_BYTES(rx_start_ptr); rx_end_ptr = SWAP_TWO_BYTES(rx_end_ptr);

    // 2. read the data (and handle different cases)
    uint16_t len_of_received_data;
    // no data
    if (rx_end_ptr == rx_start_ptr) {
        return 0;
    }
    // incoming data have reached the end of HW RX buffer and a remaining data were written
    // at the start of HW RX buffer
    else if (rx_end_ptr < rx_start_ptr) {
        len_of_received_data = (0xFFFF-rx_start_ptr) + (rx_end_ptr-0x0000);
        if (len_of_received_data > buf_size) {
            printf("Received data is bigger than buffer\n");
            return 0;
        }
        // we need to read the data in 2 steps: [from start to 0xFFFF] and [from 0x0000 to end]
        _read_spi(sock->_host_wiznet, rx_start_ptr, sock_n_rx_buffer, buf, 0xFFFF-rx_start_ptr);
        _read_spi(sock->_host_wiznet, 0x0000, sock_n_rx_buffer, buf+0xFFFF-rx_start_ptr, rx_end_ptr-0x0000);
    }
    // standard case
    else {
        len_of_received_data = rx_end_ptr-rx_start_ptr;
        if (len_of_received_data > buf_size) {
            printf("Received data is bigger than buffer\n");
            return 0;
        }
        _read_spi(sock->_host_wiznet, rx_start_ptr, sock_n_rx_buffer, buf, len_of_received_data);
    }

    // 3. update the pointer to the end of data in RX buffer
    rx_end_ptr = SWAP_TWO_BYTES(rx_end_ptr);
    _write_spi(sock->_host_wiznet, Sn_RX_RD, sock_n_register, (uint8_t *)&rx_end_ptr, sizeof(uint16_t));

    // 4. send RECV command to notify Wiznet chip
    uint8_t byte = SOCK_CMD_RECV;
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    return len_of_received_data;
}


/*
 *  Read data from HW RX buffer of socket 'sock' into automatically allocated buffer associated with
 *  pointer 'buf'. Length of data is determining and returning as uint16_t value. You can reuse the
 *  same pointer but should free() it after last time
 */
uint16_t recv_alloc(socket_t *sock, uint8_t **buf) {
    // TODO: case when we do not read incoming data for a long time

    // choose appropriate socket register and RX buffer
    uint8_t sock_n_register = sock_n_registers[sock->_id];
    uint16_t sock_n_rx_buffer = sock_n_rx_buffers[sock->_id];

    // 1. read start and end pointers of RX buffer with our data
    uint16_t rx_start_ptr, rx_end_ptr;
    _read_spi(sock->_host_wiznet, Sn_RX_RD, sock_n_register, (uint8_t *)&rx_start_ptr, sizeof(uint16_t));
    _read_spi(sock->_host_wiznet, Sn_RX_WR, sock_n_register, (uint8_t *)&rx_end_ptr, sizeof(uint16_t));
    rx_start_ptr = SWAP_TWO_BYTES(rx_start_ptr); rx_end_ptr = SWAP_TWO_BYTES(rx_end_ptr);

    // 2. read the data (and handle different cases)
    uint16_t len_of_received_data;
    // no data
    if (rx_end_ptr == rx_start_ptr) {
        return 0;
    }
    // incoming data have reached the end of HW RX buffer and a remaining data were written
    // at the start of HW RX buffer
    else if (rx_end_ptr < rx_start_ptr) {
        len_of_received_data = (0xFFFF-rx_start_ptr) + (rx_end_ptr-0x0000);
        *buf = realloc(*buf, len_of_received_data*sizeof(uint8_t));
        // we need to read the data in 2 steps: [from start to 0xFFFF] and [from 0x0000 to end]
        _read_spi(sock->_host_wiznet, rx_start_ptr, sock_n_rx_buffer, *buf, 0xFFFF-rx_start_ptr);
        _read_spi(sock->_host_wiznet, 0x0000, sock_n_rx_buffer, *buf+0xFFFF-rx_start_ptr, rx_end_ptr-0x0000);
    }
    // normal case
    else {
        len_of_received_data = rx_end_ptr-rx_start_ptr;
        *buf = realloc(*buf, len_of_received_data*sizeof(uint8_t));
        _read_spi(sock->_host_wiznet, rx_start_ptr, sock_n_rx_buffer, *buf, len_of_received_data);
    }

    // 3. update the pointer to the end of data in RX buffer
    rx_end_ptr = SWAP_TWO_BYTES(rx_end_ptr);
    _write_spi(sock->_host_wiznet, Sn_RX_RD, sock_n_register, (uint8_t *)&rx_end_ptr, sizeof(uint16_t));

    // 4. send RECV command to notify Wiznet chip
    uint8_t byte = SOCK_CMD_RECV;
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    return len_of_received_data;
}



/*
 *  Initiate disconnection process for TCP socket 'sock'
 */
void sock_discon(socket_t *sock) {

    // choose appropriate socket register
    uint8_t sock_n_register = sock_n_registers[sock->_id];

    // disconnect TCP socket
    uint8_t byte = SOCK_CMD_DISCON;
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    // check status
    uint8_t status;
    uint32_t timeout_start = _millis();
    while (1) {
        // read status
        _read_spi(sock->_host_wiznet, Sn_SR, sock_n_register, &status, sizeof(uint8_t));
        // socket finally goes to the close state after successful disconnection
        if (status == SOCK_STATUS_CLOSED) {
            sock->status = SOCK_STATUS_CLOSED;
            break;
        }
        // handle timeout
        else if ((_millis()-timeout_start) == SOCK_TIMEOUT_DISCON) {
            sock->status = SOCK_STATUS_CANT_CLOSE;
            break;
        }
    }
}


/*
 *  Close socket 'sock' of any type
 */
void sock_close(socket_t *sock) {

    // choose appropriate socket register
    uint8_t sock_n_register = sock_n_registers[sock->_id];

    // close socket regardless of its type and current status
    uint8_t byte = SOCK_CMD_CLOSE;
    _write_spi(sock->_host_wiznet, Sn_CR, sock_n_register, &byte, sizeof(uint8_t));

    // check status
    uint8_t status;
    uint32_t timeout_start = _millis();
    while (1) {
        // read status
        _read_spi(sock->_host_wiznet, Sn_SR, sock_n_register, &status, sizeof(uint8_t));
        // operation is OK
        if (status == SOCK_STATUS_CLOSED) {
            sock->status = SOCK_STATUS_CLOSED;
            break;
        }
        // handle timeout
        else if ((_millis()-timeout_start) == SOCK_TIMEOUT_CLOSE) {
            sock->status = SOCK_STATUS_CANT_CLOSE;
            break;
        }
    }
}
