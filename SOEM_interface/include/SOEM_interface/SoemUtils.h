#ifndef SOEMUTILS_H
#define SOEMUTILS_H

#include <filesystem>
#include <cstdint>
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

enum class FirmwareFlashError {
    None = 0, InvalidFile = 1, InvalidSlave = 2, OpenFailed = 3,
    ScanFailed = 4, PreOpFailed = 5, InitFailed = 6,
    InvalidBootMailbox = 7, BootMailboxWriteFailed = 8,
    BootFailed = 9, TransferFailed = 10, ActivationFailed = 11,
    InvalidPackage = 12
};

struct FirmwareFlashResult {
    FirmwareFlashError error{FirmwareFlashError::None};
    uint16_t slave{0};
    int soemReturn{0};
    uint32_t foeError{0};
    uint16_t mailboxError{0};
    uint16_t alState{0}, alStatusCode{0};
    bool alStateValid{false};
    bool downloadComplete{false}, activationRequested{false};
    std::string reason;
    std::string message;
};

// Pure presentation helper: does not access the NIC or the slave.
SOEM_INTERFACE_EXPORT std::string formatFirmwareFlashResult(const FirmwareFlashResult& result);

class SOEM_INTERFACE_EXPORT FirmwareTool
{
public:
    using ProgressCallback = std::function<void(int)>;

    explicit FirmwareTool(const std::string& ifname);
    ~FirmwareTool();

    bool init();
    void close();
    bool flashFirmware(uint16_t slave, const std::filesystem::path& binFile);
    // Remains available after close(); SOEM's raw return and peer FoE code are
    // separate fields. Success confirms download/request, not installed version.
    const FirmwareFlashResult& lastResult() const { return result_; }

private:
    bool loadFile(const std::filesystem::path& file, std::vector<uint8_t>& buffer);
    bool enterBootMode(uint16_t slave);
    bool leaveBootMode(uint16_t slave);
    bool configBootMailbox(uint16_t slave);
    bool fail(FirmwareFlashError code, const std::string& reason, int soemReturn = 0);
    bool confirmState(uint16_t slave, uint16_t wanted, int timeout);

private:
    ecx_contextt ctx_{};
    std::string ifname_;
    bool inited_{false};
    FirmwareFlashResult result_;
};
}
#endif // SOEMUTILS_H
