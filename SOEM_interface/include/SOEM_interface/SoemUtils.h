#ifndef SOEMUTILS_H
#define SOEMUTILS_H

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "SOEM_interface/soem_interface_export.h"
#include "soem/soem.h"

#define MAXBUF          524288
#define STDBUF          2048
#define MINBUF          128
#define CRCBUF          14

#define MODE_NONE       0
#define MODE_READBIN    1
#define MODE_READINTEL  2
#define MODE_WRITEBIN   3
#define MODE_WRITEINTEL 4
#define MODE_WRITEALIAS 5
#define MODE_INFO       6

#define MAXSLENGTH      256

#define IHEXLENGTH 0x20

namespace soem_interface{

class SOEM_INTERFACE_EXPORT SoemUtils
{
public:
    struct AdapterInfo
    {
        std::string name;
        std::string desc;
    };

    static std::vector<AdapterInfo> scanAdapters();
};

namespace error {
    enum SoemInterfaceErrorCode
    {
        NoError,
        InvalidSlave,
        InvalidNicName,
        NoSlaveFound,
        EcatInitFailed,
        RequestOpFailed,
        RxPdoSizeMismatch,
        TxPdoSizeMismatch,
        EthercatNotOperational,
        InvaidEEpromHexFile,
        InvaidEEpromBinFile
    };
}

class SOEM_INTERFACE_EXPORT EEpromTool
{
public:
    explicit EEpromTool(std::string ifname, int slave, int mode, std::string fname)
        : slave_(slave), mode_(mode), ifname_(std::move(ifname)), fname_(std::move(fname)) {}
    EEpromTool() = delete;

    error::SoemInterfaceErrorCode work(std::string ifname, int slave, int mode, std::string fname);

private:
    void calc_crc(uint8* crc, uint8 b);
    uint16 SIIcrc(uint8* buf);
    int input_bin(const char* fname, int* length);
    int input_intelhex(const char* fname, int* start, int* length);
    int output_bin(const char* fname, int length);
    int output_intelhex(const char* fname, int length);
    int eeprom_read(int slave, int start, int length);
    int eeprom_write(int slave, int start, int length);
    int eeprom_writealias(int slave, int alias, uint16 crc);

private:
    uint8 ebuf[MAXBUF]{};
    uint8 ob{};
    uint16 ow{};
    int os{};
    int slave_{};
    int alias{};
    ec_timet tstart{}, tend{}, tdif{};
    int wkc{};
    int mode_{};
    char sline[MAXSLENGTH]{};
    ecx_contextt ctx{};
    std::string ifname_, fname_;
};

class SOEM_INTERFACE_EXPORT FirmwareTool
{
public:
    using ProgressCallback = std::function<void(int)>;

    explicit FirmwareTool(const std::string& ifname);
    ~FirmwareTool();

    bool init();
    void close();
    bool flashFirmware(uint16_t slave, const std::filesystem::path& binFile);

private:
    bool loadFile(const std::filesystem::path& file, std::vector<uint8_t>& buffer);
    bool enterBootMode(uint16_t slave);
    bool leaveBootMode(uint16_t slave);
    bool configBootMailbox(uint16_t slave);

private:
    ecx_contextt ctx_{};
    std::string ifname_;
    bool inited_{false};
};
}
#endif // SOEMUTILS_H
