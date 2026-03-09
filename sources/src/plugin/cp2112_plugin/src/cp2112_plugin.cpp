/*
 * CP2112 Plugin – core lifecycle and top-level command handlers
 *
 * The CP2112 is a USB-HID-to-I2C/SMBus bridge with 8 GPIO pins.
 * Two modules are exposed: I2C and GPIO.
 * Neither module requires open before INFO/setParams — only before data ops.
 */

#include "cp2112_plugin.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "CP2112     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED CP2112Plugin* pluginEntry()
    {
        return new CP2112Plugin();
    }

    EXPORTED void pluginExit(CP2112Plugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const CP2112Plugin::IniValues* getAccessIniValues(const CP2112Plugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::doInit(void* /*pvUserData*/)
{
    // Seed pending configs from INI values so that an open without
    // explicit parameters uses whatever was in the config file.
    m_sI2cCfg.clockHz = m_sIniValues.u32I2cClockHz;
    m_sI2cCfg.address = m_sIniValues.u8I2cAddress;

    m_bIsInitialized = true;

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Initialized — CP2112 (VID 0x10C4 / PID 0xEA90)");
              LOG_STRING("device index:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

void CP2112Plugin::doCleanup()
{
    if (m_pI2C)  { m_pI2C->close();  m_pI2C.reset();  }
    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//              DRIVER INSTANCE ACCESSORS                        //
///////////////////////////////////////////////////////////////////

CP2112* CP2112Plugin::m_i2c() const
{
    if (!m_pI2C || !m_pI2C->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("I2C not open — call CP2112.I2C open [addr=0xNN] [clock=N]"));
        return nullptr;
    }
    return m_pI2C.get();
}

CP2112Gpio* CP2112Plugin::m_gpio() const
{
    if (!m_pGPIO || !m_pGPIO->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("GPIO not open — call CP2112.GPIO open [device=N] [dir=0xNN] ..."));
        return nullptr;
    }
    return m_pGPIO.get();
}

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<CP2112Plugin>*
CP2112Plugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
CP2112Plugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    if (it == m_mapSpeedsMaps.end()) return nullptr;
    return it->second;
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::setModuleSpeed(const std::string& module, size_t hz) const
{
    if (module == "I2C") {
        m_sI2cCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pI2C && m_pI2C->is_open()) {
            m_pI2C->close();
            auto s = m_pI2C->open(m_sI2cCfg.address,
                                  m_sI2cCfg.clockHz,
                                  m_sIniValues.u8DeviceIndex);
            if (s != CP2112::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("I2C reopen at new clock failed, hz="); LOG_UINT32(hz));
                m_pI2C.reset();
                return false;
            }
            LOG_PRINT(LOG_INFO, LOG_HDR;
                      LOG_STRING("I2C clock updated to"); LOG_UINT32(hz); LOG_STRING("Hz"));
        } else {
            LOG_PRINT(LOG_INFO, LOG_HDR;
                      LOG_STRING("I2C pending clock stored:"); LOG_UINT32(hz); LOG_STRING("Hz"));
        }
        return true;
    }

    if (module == "GPIO") {
        // CP2112 GPIO has no numeric data-rate; the only "clock" is the
        // optional clock-output on GPIO.6, which is set via clockDivider in
        // the GpioConfig.  Direct frequency manipulation is not supported here.
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("GPIO has no speed setting (use cfg clkdiv=N for clock output)"));
        return false;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("setModuleSpeed: unknown module:"); LOG_STRING(module));
    return false;
}

///////////////////////////////////////////////////////////////////
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_CP2112_INFO(const std::string& /*args*/) const
{
    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version  :"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build    :"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device   : CP2112 (USB-HID I2C/GPIO bridge, VID 0x10C4 / PID 0xEA90)"));
    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("DevIndex :"); LOG_UINT32(m_sIniValues.u8DeviceIndex));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("I2C      :"); LOG_STRING(m_pI2C && m_pI2C->is_open() ? "open" : "closed");
              LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock=");  LOG_UINT32(m_sI2cCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("GPIO     :"); LOG_STRING(m_pGPIO && m_pGPIO->is_open() ? "open" : "closed");
              LOG_STRING("dir=0x");     LOG_HEX8(m_sGpioCfg.directionMask);
              LOG_STRING("pp=0x");      LOG_HEX8(m_sGpioCfg.pushPullMask);
              LOG_STRING("special=0x"); LOG_HEX8(m_sGpioCfg.specialFuncMask));

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Commands : INFO I2C GPIO"));
    return true;
}

bool CP2112Plugin::m_CP2112_I2C(const std::string& args) const
{
    return generic_module_dispatch<CP2112Plugin>(this, "I2C", args);
}

bool CP2112Plugin::m_CP2112_GPIO(const std::string& args) const
{
    return generic_module_dispatch<CP2112Plugin>(this, "GPIO", args);
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_LocalSetParams(const PluginDataSet* ps)
{
    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    const auto& m = ps->mapSettings;
    bool ok = true;

    auto getString = [&](const char* key, std::string& dst) {
        if (m.count(key)) dst = m.at(key);
    };
    auto getU32 = [&](const char* key, uint32_t& dst) {
        if (m.count(key)) ok &= numeric::str2uint32(m.at(key), dst);
    };
    auto getU8 = [&](const char* key, uint8_t& dst) {
        if (m.count(key)) ok &= numeric::str2uint8(m.at(key), dst);
    };

    getString(ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    getU8   (DEVICE_INDEX,    m_sIniValues.u8DeviceIndex);
    getU32  (I2C_CLOCK,       m_sIniValues.u32I2cClockHz);
    getU8   (I2C_ADDRESS,     m_sIniValues.u8I2cAddress);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}
