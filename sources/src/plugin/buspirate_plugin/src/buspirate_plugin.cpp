
#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"

#include "uUart.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                 LOG DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BUSPIRATE  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    UART_PORT          "UART_PORT"
#define    BAUDRATE           "BAUDRATE"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"
#define    READ_BUF_TIMEOUT   "READ_BUF_TIMEOUT"
#define    SCRIPT_DELAY       "SCRIPT_DELAY"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED BuspiratePlugin* pluginEntry()
    {
        return new BuspiratePlugin();
    }

    EXPORTED void pluginExit( BuspiratePlugin *ptrPlugin )
    {
        if (nullptr != ptrPlugin )
        {
            delete ptrPlugin;
        }
    }
}

///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool BuspiratePlugin::doInit(void *pvUserData)
{
    if (m_sIniValues.strUartPort.empty() || m_sIniValues.u32UartBaudrate == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; 
                LOG_STRING("Missing UART settings: Port []"); 
                LOG_STRING(m_sIniValues.strUartPort); 
                LOG_STRING("Baudrate:"); 
                LOG_UINT32(m_sIniValues.u32UartBaudrate));
        return false;
    }
    drvUart.open(m_sIniValues.strUartPort, m_sIniValues.u32UartBaudrate);

    return m_bIsInitialized = drvUart.is_open();
}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void BuspiratePlugin::doCleanup(void)
{
    if (true == m_bIsInitialized)
    {
        drvUart.close();
    }

    m_bIsInitialized = false;
    m_bIsEnabled     = false;

}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       BUSPIRATE.INFO
  *
  * \param[in] args NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool BuspiratePlugin::m_Buspirate_INFO (const std::string &args) const
{
    bool bRetVal = false;

    do {
        // expected no arguments
        if (false == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_EMPTY, LOG_STRING(BUSPIRATE_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: BusPirate multi-protocol interface (SPI/I2C/UART/1-Wire/RawWire)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Port:"); LOG_STRING(m_sIniValues.strUartPort);
                             LOG_STRING("  Baud:"); LOG_UINT32(m_sIniValues.u32UartBaudrate));

        // ── MODE ─────────────────────────────────────────────────────────────
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("MODE : switch the Bus Pirate into a binary protocol mode (must be called before any protocol command)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args : bitbang | spi | i2c | uart | onewire | rawwire | jtag | reset | exit"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: BUSPIRATE.MODE bitbang      - enter raw bitbang mode (BBIO1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE spi          - enter SPI mode (SPI1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE i2c          - enter I2C mode (I2C1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE uart         - enter UART mode (ART1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE onewire      - enter 1-Wire mode (1W01)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE rawwire      - enter raw-wire mode (RAW1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE jtag         - enter JTAG mode (JTG1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE reset        - send reset command (0x0F)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("         BUSPIRATE.MODE exit         - exit current mode, return to bitbang"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : only one protocol is active at a time; call MODE again to switch"));

        // ── SPI ───────────────────────────────────────────────────────────────
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI : full-duplex SPI bus (prerequisite: MODE spi)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : configure SPI bus settings"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : z/V  l/H  i/A  m/E   (see help for bit meanings)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           z=HiZ(0)! V=3.3V(1)  l=CKP-low(0)! H=CKP-high(1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           i=CKE-Idle2Active(0) A=CKE-Active2Idle(1)  m=SMP-middle(0)! E=SMP-end(1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI cfg Vli      - 3.3V, CKP low, CKE Idle2Active, SMP middle"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.SPI cfg ?        - print current cfg byte"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : drive the chip-select pin"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : en | dis   (en = assert CS low / GND, dis = deassert CS 3.3V/HiZ)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI cs en"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.SPI cs dis"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  per : configure peripherals (power / pull-ups / AUX / CS)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : w=power(0/1)  x=pullups(0/1)  y=AUX(0/1)  z=CS(0/1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI per wxyz      - e.g. per 1100 = power+pull-ups on"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set SPI clock frequency"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 30kHz | 125kHz | 250kHz | 1MHz | 2MHz | 2.6MHz | 4MHz | 8MHz"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI speed 1MHz"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : bulk SPI transfer (1-16 bytes); MISO bytes are returned"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes as hex string)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI write DEADBEEF   - send 4 bytes, print MISO"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive bytes (clocks dummy 0x00 on MOSI, 1-16 bytes)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count, 1-16)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI read 4"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  sniff : hardware SPI sniffer"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : all | cslo   (all = sniff always, cslo = sniff when CS low)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI sniff all    - start sniffing all SPI traffic"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.SPI sniff cslo   - sniff only when CS is asserted low"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : send any byte to exit sniffer; MODE LED turns off if data overflows"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one CS-asserted transaction (0-4096 bytes each)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI wrrd 9F:3    - send cmd 0x9F, read 3 bytes"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI wrrdf flash_cmd.bin"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.SPI script read_flash.txt"));

        // ── I2C ───────────────────────────────────────────────────────────────
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C bus master (prerequisite: MODE i2c)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set I2C clock speed"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 5KHz | 50kHz | 100kHz | 400kHz"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C speed 100kHz"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  bit : send a single bus-control sequence"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : start | stop | ack | nack"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C bit start     - send START condition"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.I2C bit stop      - send STOP condition"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.I2C bit ack       - clock out ACK bit"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.I2C bit nack      - clock out NACK bit"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : use for manual framing when wrrd is not sufficient"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : bulk I2C write (1-16 bytes); ACK/NACK status printed"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C write A050   - write address 0xA0 + register 0x50"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status per byte"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes (ACKs all except the last, which gets NACK + STOP)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C read 2       - read 2 bytes"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read (0-4096 bytes each; uses I2C write-then-read command 0x08)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C wrrd A050:2  - write 0xA0 0x50, read 2 bytes"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using files from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C wrrdf i2c_seq.bin"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  sniff : sniff I2C bus traffic"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : on | off"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C sniff on     - start sniffer ([/] = start/stop, +/- = ACK/NACK)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.I2C sniff off    - stop sniffer"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : extended AUX / CS pin control (command 0x09)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : acl | ach | acz | ra | ua | uc"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           acl=AUX/CS low  ach=AUX/CS high  acz=AUX/CS HiZ"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           ra=read AUX     ua=use AUX        uc=use CS"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C aux acl      - drive AUX/CS low"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.I2C aux ra       - read AUX pin state"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  per : configure peripherals (power / pull-ups / AUX / CS)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C per wxyz     - same format as SPI per"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  mode : query current I2C mode version string from device"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C mode         - device responds I2C1"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.I2C script eeprom_test.txt"));

        // ── UART ──────────────────────────────────────────────────────────────
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("UART : UART serial interface (prerequisite: MODE uart)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set UART baud rate from preset list"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 300 | 1200 | 2400 | 4800 | 9600 | 19200 | 31250 | 38400 | 57600 | 115200"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART speed 9600"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  bdr : set custom baud rate via BRG register value"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (16-bit BRG value, hex or decimal)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           Baud = Fosc / (4 * (BRG+1)), Fosc=32MHz, BRGH=1"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART bdr 0x0340  - configure for 9600 baud"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : configure UART frame format"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : z/V  8N/8E/8O/9N  1/2  n/i"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           z=HiZ(0)  V=3.3V(1)   8N/8E/8O/9N=data+parity"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           1=1 stop bit(0)!  2=2 stop bits(1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           n=idle-1/normal(0)!  i=idle-0/inverted(1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART cfg V8N1n   - 3.3V, 8N1, normal polarity"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.UART cfg ?       - print current cfg byte"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  echo : enable/disable RX echo to USB"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : start | stop"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART echo start  - begin echoing received bytes to host"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.UART echo stop   - stop echoing (default on mode entry)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : UART is always active; echo only controls forwarding to USB"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  mode : enter transparent UART bridge"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : bridge"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART mode bridge - start bridge; unplug Bus Pirate to exit"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : bulk UART transmit (1-16 bytes)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes as hex string)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART write 48454C4C4F  - transmit 'HELLO'"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  per : configure peripherals (power / pull-ups / AUX / CS)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART per wxyz    - same format as SPI per"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.UART script uart_init.txt"));

        // ── ONEWIRE ───────────────────────────────────────────────────────────
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("ONEWIRE : Dallas/Maxim 1-Wire bus (prerequisite: MODE onewire)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  reset : send 1-Wire reset pulse"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.ONEWIRE reset    - sends reset; device presence detected"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : bulk 1-Wire write (1-16 bytes)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes as hex string)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.ONEWIRE write CC     - send SKIP ROM command"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.ONEWIRE write CC44   - SKIP ROM + Convert T (DS18B20)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from the 1-Wire bus (one byte at a time)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.ONEWIRE read 9   - read 9-byte scratchpad (DS18B20)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  search : execute ROM/ALARM search macro"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : rom | alarm"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.ONEWIRE search rom   - ROM search (0xF0); returns 8-byte addresses"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.ONEWIRE search alarm  - ALARM search (0xEC)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 8-byte device addresses until 8×0xFF terminator"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : configure peripherals (power / pull-ups / AUX / CS)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : w/W  p/P  a/A  c/C   (lower=disable, upper=enable)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           w/W=power  p/P=pull-ups  a/A=AUX  c/C=CS"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.ONEWIRE cfg WP    - enable power and pull-ups"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.ONEWIRE cfg ?     - print current cfg byte"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.ONEWIRE script ds18b20_read.txt"));

        // ── RAWWIRE ───────────────────────────────────────────────────────────
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWWIRE : bit-bang 2/3-wire protocol (prerequisite: MODE rawwire)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : configure raw-wire bus settings"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : Z/V  2/3  M/L"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           Z=HiZ(0) V=3.3V(1)   2=2-wire(0)! 3=3-wire(1)   M=MSB(0)! L=LSB(1)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE cfg V2M  - 3.3V, 2-wire, MSB first"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE cfg ?    - print current cfg byte"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set bit-bang clock speed"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 5KHz | 50kHz | 100kHz | 400kHz"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE speed 100kHz"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : drive the chip-select pin"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : low | high"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE cs low   - assert CS to GND"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE cs high  - deassert CS to 3.3V/HiZ"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  clock : control the clock line"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : tick | lo | hi | N (1-16 bulk ticks)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE clock tick  - one clock pulse (low→high→low)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE clock lo    - set clock low"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE clock hi    - set clock high"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE clock 8     - send 8 bulk clock ticks"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  data : drive the data (MOSI/SDA) pin"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : low | high"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE data low   - drive data pin to GND"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE data high  - drive data pin to 3.3V/HiZ"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  bit : send I2C-style start/stop bit, or bulk bits from a byte"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : start | stop | 0kXY  (k=0..7 bits to send, XY=hex data byte)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE bit start   - send I2C start condition"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE bit stop    - send I2C stop condition"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE bit 07A5   - clock out 8 bits of 0xA5 (MSB first)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read a bit, byte, or data input pin state"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : bit | byte | dpin"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE read byte  - read one byte from bus (writes 0xFF in 3-wire)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE read bit   - read single bit from bus"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE read dpin  - read data input pin state (no clock)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : bulk raw-wire write (1-16 bytes)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes as hex string)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE write CAFE"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  per : configure peripherals (power / pull-ups / AUX / CS)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE per wxyz  - same format as SPI per"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  pic : PIC ICSP programming extension (2-wire mode only)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : read:HEXCMD | write:HEXCMD+DATA"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           read  payload = 1 byte (XXCMD 00YYYYYY)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           write payload = 3 bytes (XXYYYYYY + 16-bit instruction)"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE pic read:04   - send 4-bit ICSP read command 0x04"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("           BUSPIRATE.RAWWIRE pic write:048000  - write cmd+instruction to PIC"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: BUSPIRATE.RAWWIRE script prog_sequence.txt"));

        bRetVal = true;

    } while(false);

    return bRetVal;

}

/**
 * \brief MODE command implementation
 *
 * \note Supported modes: bin reset spi i2c uart 1wire rawire jtag exit
 * \note Example BUSPIRATE.MODE bin
 *               BUSPIRATE.MODE spi
 *               BUSPIRATE.MODE exit
 *
 */
bool BuspiratePlugin::m_Buspirate_MODE (const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Argument expected: mode"));
            break;
        }

        // if plugin is not enabled then stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        bRetVal = m_handle_mode(args);

    } while(false);

    return bRetVal;

}

bool BuspiratePlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(ARTEFACTS_PATH) > 0) {
                m_sIniValues.strArtefactsPath = psSetParams->mapSettings.at(ARTEFACTS_PATH);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_sIniValues.strArtefactsPath));
            }

            if (psSetParams->mapSettings.count(UART_PORT) > 0) {
                m_sIniValues.strUartPort = psSetParams->mapSettings.at(UART_PORT);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Port :"); LOG_STRING(m_sIniValues.strUartPort));
            }

            if (psSetParams->mapSettings.count(BAUDRATE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(BAUDRATE), m_sIniValues.u32UartBaudrate)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Baudrate :"); LOG_UINT32(m_sIniValues.u32UartBaudrate));
            }

            if (psSetParams->mapSettings.count(READ_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_TIMEOUT), m_sIniValues.u32ReadTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout :"); LOG_UINT32(m_sIniValues.u32ReadTimeout));
            }

            if (psSetParams->mapSettings.count(WRITE_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(WRITE_TIMEOUT), m_sIniValues.u32WriteTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout :"); LOG_UINT32(m_sIniValues.u32WriteTimeout));
            }

            if (psSetParams->mapSettings.count(READ_BUF_SIZE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_BUF_SIZE), m_sIniValues.u32UartReadBufferSize)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufSize :"); LOG_UINT32(m_sIniValues.u32UartReadBufferSize));
            }

            if (psSetParams->mapSettings.count(SCRIPT_DELAY) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(SCRIPT_DELAY), m_sIniValues.u32ScriptDelay)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ScriptDelay :"); LOG_UINT32(m_sIniValues.u32ScriptDelay));
            }

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;

} /* m_LocalSetParams() */



const BuspiratePlugin::IniValues* getAccessIniValues(const BuspiratePlugin& obj)
{
    return &obj.m_sIniValues;

} /* getAccessIniValues() */
