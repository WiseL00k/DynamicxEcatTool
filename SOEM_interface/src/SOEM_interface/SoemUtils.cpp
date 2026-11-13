#include "SOEM_interface/SoemUtils.h"
#include "SOEM_interface/ErrorText.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>

extern "C" {
#include <soem/soem.h>
}

namespace soem_interface{
namespace {

// Envelope checks only: the slave remains authoritative for CRCs, image
// descriptors, release pairing and bootloader/layout compatibility.
std::string ecat2CanfdEnvelopeError(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() < 256 || std::memcmp(bytes.data(), "EC2PKG01", 8) != 0)
        return "ECAT2CANFD 需要以 EC2PKG01 开头的完整双核升级包；当前文件不是该容器，不能使用裸 M7/M4 BIN，请选择发布目录中的 ecat2canfd.bin";
    auto read32 = [&](size_t offset) {
        return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8
             | uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
    };
    if (bytes.size() > 0x60000 || read32(12) != 256 || read32(16) != bytes.size())
        return "ECAT2CANFD 容器长度或包头长度不匹配，文件可能被截断或损坏，请重新获取完整发布包";
    for (size_t entry : {size_t(48), size_t(64)}) {
        const uint32_t offset = read32(entry), size = read32(entry + 4);
        if (offset < 256 || offset > bytes.size() || size < 72 || size > bytes.size() - offset)
            return "ECAT2CANFD 容器中的双核镜像范围无效，请重新获取完整发布包";
    }
    return {};
}

} // namespace

std::vector<SoemUtils::AdapterInfo> SoemUtils::scanAdapters()
{
    std::vector<AdapterInfo> adapters;

    ec_adaptert* adapter = ec_find_adapters();
    ec_adaptert* current = adapter;

    while (current != nullptr) {
        adapters.push_back({current->name, current->desc});
        current = current->next;
    }

    ec_free_adapters(adapter);
    return adapters;
}

void EEpromTool::calc_crc(uint8* crc, uint8 b)
{
    *crc ^= b;
    for (int j = 0; j <= 7; j++) {
        if (*crc & 0x80) {
            *crc = (*crc << 1) ^ 0x07;
        } else {
            *crc = (*crc << 1);
        }
    }
}

uint16 EEpromTool::SIIcrc(uint8* buf)
{
    uint8 crc = 0xff;
    for (int i = 0; i <= 13; i++) {
        calc_crc(&crc, *(buf++));
    }
    return static_cast<uint16>(crc);
}

int EEpromTool::input_bin(const char* fname, int* length)
{
    FILE* fp = std::fopen(fname, "rb");
    if (fp == nullptr) {
        return 0;
    }

    int cc = 0;
    int c = 0;
    while (((c = std::fgetc(fp)) != EOF) && (cc < MAXBUF)) {
        ebuf[cc++] = static_cast<uint8>(c);
    }
    *length = cc;
    std::fclose(fp);

    return 1;
}

int EEpromTool::input_intelhex(const char* fname, int* start, int* length)
{
    FILE* fp = std::fopen(fname, "r");
    if (fp == nullptr) {
        return 0;
    }

    int c = 0;
    int retval = 1;
    int hstart = MAXBUF;
    int hlength = 0;

    do {
        std::memset(sline, 0x00, MAXSLENGTH);
        int sc = 0;
        while (((c = std::fgetc(fp)) != EOF) && (c != 0x0A) && (sc < (MAXSLENGTH - 1))) {
            sline[sc++] = static_cast<uint8>(c);
        }
        if ((c != EOF) && ((sc < 11) || (sline[0] != ':'))) {
            c = EOF;
            retval = 0;
            std::printf("Invalid Intel Hex format.\n");
        }
        if (c != EOF) {
            int ll = 0;
            int ladr = 0;
            int lt = 0;
            int sn = std::sscanf(sline, ":%2x%4x%2x", &ll, &ladr, &lt);
            if ((sn == 3) && ((ladr + ll) <= MAXBUF) && (lt == 0)) {
                int sum = ll + (ladr >> 8) + (ladr & 0xff) + lt;
                if (ladr < hstart) {
                    hstart = ladr;
                }
                int i = 0;
                for (; i < ll; i++) {
                    int lval = 0;
                    sn = std::sscanf(&sline[9 + (i << 1)], "%2x", &lval);
                    if (sn != 1) {
                        c = EOF;
                        retval = 0;
                        break;
                    }
                    ebuf[ladr + i] = static_cast<uint8>(lval);
                    sum += static_cast<uint8>(lval);
                }
                if (((ladr + ll) - hstart) > hlength) {
                    hlength = (ladr + ll) - hstart;
                }
                sum = (0x100 - sum) & 0xff;
                int lval = 0;
                sn = std::sscanf(&sline[9 + (i << 1)], "%2x", &lval);
                if (!sn || ((sum - lval) != 0)) {
                    c = EOF;
                    retval = 0;
                    std::printf("Invalid checksum.\n");
                }
            }
        }
    } while (c != EOF);

    if (retval) {
        *length = hlength;
        *start = hstart;
    }
    std::fclose(fp);

    return retval;
}

int EEpromTool::output_bin(const char* fname, int length)
{
    FILE* fp = std::fopen(fname, "wb");
    if (fp == nullptr) {
        return 0;
    }
    for (int cc = 0; cc < length; cc++) {
        std::fputc(ebuf[cc], fp);
    }
    std::fclose(fp);

    return 1;
}

int EEpromTool::output_intelhex(const char* fname, int length)
{
    FILE* fp = std::fopen(fname, "w");
    if (fp == nullptr) {
        return 0;
    }
    int cc = 0;
    while (cc < length) {
        int ll = length - cc;
        if (ll > IHEXLENGTH) {
            ll = IHEXLENGTH;
        }
        int sum = ll + (cc >> 8) + (cc & 0xff);
        std::fprintf(fp, ":%2.2X%4.4X00", ll, cc);
        for (int i = 0; i < ll; i++) {
            std::fprintf(fp, "%2.2X", ebuf[cc + i]);
            sum += ebuf[cc + i];
        }
        std::fprintf(fp, "%2.2X\n", (0x100 - sum) & 0xff);
        cc += ll;
    }
    std::fprintf(fp, ":00000001FF\n");
    std::fclose(fp);

    return 1;
}

int EEpromTool::eeprom_read(int slave, int start, int length)
{
    int ainc = 4;
    uint16 estat = 0;
    uint16 aiadr = 0;
    uint8 eepctl = 0;

    if ((ctx.slavecount >= slave) && (slave > 0) && ((start + length) <= MAXBUF)) {
        aiadr = 1 - slave;
        eepctl = 2;
        ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET);
        eepctl = 0;
        ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET);

        estat = 0x0000;
        aiadr = 1 - slave;
        ecx_APRD(&ctx.port, aiadr, ECT_REG_EEPSTAT, sizeof(estat), &estat, EC_TIMEOUTRET);
        estat = etohs(estat);
        if (estat & EC_ESTAT_R64) {
            ainc = 8;
            for (int i = start; i < (start + length); i += ainc) {
                const uint64 b8 = ecx_readeepromAP(&ctx, aiadr, i >> 1, EC_TIMEOUTEEP);
                ebuf[i] = b8 & 0xFF;
                ebuf[i + 1] = (b8 >> 8) & 0xFF;
                ebuf[i + 2] = (b8 >> 16) & 0xFF;
                ebuf[i + 3] = (b8 >> 24) & 0xFF;
                ebuf[i + 4] = (b8 >> 32) & 0xFF;
                ebuf[i + 5] = (b8 >> 40) & 0xFF;
                ebuf[i + 6] = (b8 >> 48) & 0xFF;
                ebuf[i + 7] = (b8 >> 56) & 0xFF;
            }
        } else {
            for (int i = start; i < (start + length); i += ainc) {
                const uint32 b4 = ecx_readeepromAP(&ctx, aiadr, i >> 1, EC_TIMEOUTEEP) & 0xFFFFFFFF;
                ebuf[i] = b4 & 0xFF;
                ebuf[i + 1] = (b4 >> 8) & 0xFF;
                ebuf[i + 2] = (b4 >> 16) & 0xFF;
                ebuf[i + 3] = (b4 >> 24) & 0xFF;
            }
        }

        return 1;
    }

    return 0;
}

int EEpromTool::eeprom_write(int slave, int start, int length)
{
    if ((ctx.slavecount >= slave) && (slave > 0) && ((start + length) <= MAXBUF)) {
        uint16 aiadr = 1 - slave;
        uint8 eepctl = 2;
        ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET);
        eepctl = 0;
        ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET);

        aiadr = 1 - slave;
        uint16* wbuf = reinterpret_cast<uint16*>(&ebuf[0]);
        int dc = 0;
        for (int i = start; i < (start + length); i += 2) {
            ecx_writeeepromAP(&ctx, aiadr, i >> 1, *(wbuf + (i >> 1)), EC_TIMEOUTEEP);
            if (++dc >= 100) {
                dc = 0;
                std::printf(".");
                std::fflush(stdout);
            }
        }

        return 1;
    }

    return 0;
}

int EEpromTool::eeprom_writealias(int slave, int aliasValue, uint16 crc)
{
    if ((ctx.slavecount >= slave) && (slave > 0) && (aliasValue <= 0xffff)) {
        uint16 aiadr = 1 - slave;
        uint8 eepctl = 2;
        ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET);
        eepctl = 0;
        ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET);

        int ret = ecx_writeeepromAP(&ctx, aiadr, 0x04, aliasValue, EC_TIMEOUTEEP);
        if (ret) {
            ret = ecx_writeeepromAP(&ctx, aiadr, 0x07, crc, EC_TIMEOUTEEP);
        }

        return ret;
    }

    return 0;
}

error::SoemInterfaceErrorCode EEpromTool::work(std::string ifname, int slave, int mode, std::string fname)
{
    if (!ecx_init(&ctx, ifname.c_str())) {
        std::printf("No socket connection on %s\nExcecute as root\n", ifname.c_str());
        return error::EcatInitFailed;
    }

    error::SoemInterfaceErrorCode result = error::NoError;
    std::printf("ecx_init on %s succeeded.\n", ifname.c_str());

    uint16 w = 0x0000;
    wkc = ecx_BRD(&ctx.port, 0x0000, ECT_REG_TYPE, sizeof(w), &w, EC_TIMEOUTSAFE);
    if (wkc <= 0) {
        std::printf("No slaves found!\n");
        result = error::NoSlaveFound;
    } else {
        ctx.slavecount = wkc;
        std::printf("%d slaves found.\n", ctx.slavecount);

        if ((ctx.slavecount < slave) || (slave <= 0)) {
            std::printf("Slave number outside range.\n");
            result = error::InvalidSlave;
        } else {
            int estart = 0;
            int esize = 0;
            uint16* wbuf = nullptr;

            if ((mode == MODE_INFO) || (mode == MODE_READBIN) || (mode == MODE_READINTEL)) {
                tstart = osal_current_time();
                if (!eeprom_read(slave, 0x0000, MINBUF)) {
                    result = error::InvalidSlave;
                } else {
                    wbuf = reinterpret_cast<uint16*>(&ebuf[0]);
                    std::printf("Slave %d data\n", slave);
                    std::printf(" PDI Control      : %4.4X\n", *(wbuf + 0x00));
                    std::printf(" PDI Config       : %4.4X\n", *(wbuf + 0x01));
                    std::printf(" Config Alias     : %4.4X\n", *(wbuf + 0x04));
                    std::printf(" Checksum         : %4.4X\n", *(wbuf + 0x07));
                    std::printf("   calculated     : %4.4X\n", SIIcrc(&ebuf[0]));
                    std::printf(" Vendor ID        : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x08));
                    std::printf(" Product Code     : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x0A));
                    std::printf(" Revision Number  : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x0C));
                    std::printf(" Serial Number    : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x0E));
                    std::printf(" Mailbox Protocol : %4.4X\n", *(wbuf + 0x1C));
                    esize = (*(wbuf + 0x3E) + 1) * 128;
                    if (esize > MAXBUF) {
                        esize = MAXBUF;
                    }
                    std::printf(" Size             : %4.4X = %d bytes\n", *(wbuf + 0x3E), esize);
                    std::printf(" Version          : %4.4X\n", *(wbuf + 0x3F));
                }
            }

            if (result == error::NoError && ((mode == MODE_READBIN) || (mode == MODE_READINTEL))) {
                if (esize > MINBUF) {
                    eeprom_read(slave, MINBUF, esize - MINBUF);
                }

                tend = osal_current_time();
                osal_time_diff(&tstart, &tend, &tdif);
                if (mode == MODE_READINTEL) {
                    output_intelhex(fname.c_str(), esize);
                }
                if (mode == MODE_READBIN) {
                    output_bin(fname.c_str(), esize);
                }

                std::printf("\nTotal EEPROM read time :%dms\n", static_cast<int>(tdif.tv_sec * 1000 + tdif.tv_nsec / 1000000));
            }

            if (result == error::NoError && ((mode == MODE_WRITEBIN) || (mode == MODE_WRITEINTEL))) {
                int rc = 0;
                if (mode == MODE_WRITEINTEL) {
                    rc = input_intelhex(fname.c_str(), &estart, &esize);
                }
                if (mode == MODE_WRITEBIN) {
                    rc = input_bin(fname.c_str(), &esize);
                }

                if (rc > 0) {
                    wbuf = reinterpret_cast<uint16*>(&ebuf[0]);
                    std::printf("Slave %d\n", slave);
                    std::printf(" Vendor ID        : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x08));
                    std::printf(" Product Code     : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x0A));
                    std::printf(" Revision Number  : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x0C));
                    std::printf(" Serial Number    : %8.8X\n", *reinterpret_cast<uint32*>(wbuf + 0x0E));

                    std::printf("Busy");
                    std::fflush(stdout);
                    tstart = osal_current_time();
                    if (!eeprom_write(slave, estart, esize)) {
                        result = error::InvalidSlave;
                    }
                    tend = osal_current_time();
                    osal_time_diff(&tstart, &tend, &tdif);

                    std::printf("\nTotal EEPROM write time :%dms\n", static_cast<int>(tdif.tv_sec * 1000 + tdif.tv_nsec / 1000000));
                } else {
                    std::printf("Error reading file, abort.\n");
                    result = (mode == MODE_WRITEINTEL) ? error::InvaidEEpromHexFile : error::InvaidEEpromBinFile;
                }
            }

            if (result == error::NoError && mode == MODE_WRITEALIAS) {
                if (eeprom_read(slave, 0x0000, CRCBUF)) {
                    wbuf = reinterpret_cast<uint16*>(&ebuf[0]);
                    *(wbuf + 0x04) = alias;
                    if (eeprom_writealias(slave, alias, SIIcrc(&ebuf[0]))) {
                        std::printf("Alias %4.4X written successfully to slave %d\n", alias, slave);
                    } else {
                        std::printf("Alias not written\n");
                        result = error::InvalidSlave;
                    }
                } else {
                    std::printf("Could not read slave EEPROM");
                    result = error::InvalidSlave;
                }
            }
        }
    }

    std::printf("End, close socket\n");
    ecx_close(&ctx);
    return result;
}

FirmwareTool::FirmwareTool(const std::string& ifname)
    : ifname_(ifname)
{
    std::memset(&ctx_, 0, sizeof(ctx_));
}

FirmwareTool::~FirmwareTool()
{
    close();
}

bool FirmwareTool::init()
{
    if (inited_) {
        return true;
    }

    if (!ecx_init(&ctx_, ifname_.c_str())) {
        return fail(FirmwareFlashError::OpenFailed, "打开网卡失败，请检查网卡名称和原始套接字权限");
    }
    inited_ = true;

    const int count = ecx_config_init(&ctx_);
    if (count <= 0) {
        fail(FirmwareFlashError::ScanFailed, "未能完成从站初始化", count);
        close();
        return false;
    }

    if (!confirmState(0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE * 4)) {
        fail(FirmwareFlashError::PreOpFailed, "初始化后未确认全部从站处于 PRE-OP");
        close();
        return false;
    }
    return true;
}

bool FirmwareTool::confirmState(uint16_t slave, uint16_t wanted, int timeout)
{
    const auto state = ecx_statecheck(&ctx_, slave, wanted, timeout);
    // SOEM returns only the low nibble: BOOT+ERROR otherwise looks like BOOT.
    return state == wanted && (ctx_.slavelist[slave].state & EC_STATE_ERROR) == 0;
}

bool FirmwareTool::fail(FirmwareFlashError code, const std::string& reason, int soemReturn)
{
    result_.error = code;
    result_.reason = reason;
    result_.soemReturn = soemReturn;
    if (inited_) {
        ec_errort error{};
        while (ecx_poperror(&ctx_, &error)) {
            if (error.Slave != 0 && error.Slave != result_.slave) continue;
            if (error.Etype == EC_ERR_TYPE_FOE_ERROR)
                result_.foeError = static_cast<uint32_t>(error.AbortCode);
            if (error.Etype == EC_ERR_TYPE_MBX_ERROR)
                result_.mailboxError = error.ErrorCode;
        }
        if (result_.slave > 0 && result_.slave <= ctx_.slavecount) {
            ec_alstatust state{};
            const int wkc = ecx_FPRD(&ctx_.port, ctx_.slavelist[result_.slave].configadr,
                                    ECT_REG_ALSTAT, sizeof(state), &state, EC_TIMEOUTRET);
            result_.alStateValid = wkc == 1;
            if (result_.alStateValid) {
                result_.alState = etohs(state.alstatus);
                result_.alStatusCode = etohs(state.alstatuscode);
            }
        }
    }
    result_.message = formatFirmwareFlashResult(result_);
    return false;
}

void FirmwareTool::close()
{
    if (inited_) {
        ecx_close(&ctx_);
        inited_ = false;
    }
}

bool FirmwareTool::loadFile(const std::filesystem::path& file, std::vector<uint8_t>& buffer)
{
    std::ifstream ifs(file, std::ios::binary);
    if (!ifs) {
        return false;
    }

    ifs.seekg(0, std::ios::end);
    const std::streampos end = ifs.tellg();
    if (end <= 0 || end > (std::numeric_limits<int>::max)()) {
        return false;
    }
    const auto size = static_cast<size_t>(end);
    ifs.seekg(0);

    buffer.resize(size);
    if (size == 0) {
        return true;
    }

    ifs.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(ifs);
}

bool FirmwareTool::configBootMailbox(uint16_t slave)
{
    const uint32_t rx = ecx_readeeprom(&ctx_, slave, ECT_SII_BOOTRXMBX, EC_TIMEOUTEEP);
    const uint32_t tx = ecx_readeeprom(&ctx_, slave, ECT_SII_BOOTTXMBX, EC_TIMEOUTEEP);
    const uint16_t rxAddress = LO_WORD(rx), rxSize = HI_WORD(rx);
    const uint16_t txAddress = LO_WORD(tx), txSize = HI_WORD(tx);
    if (!rxAddress || !txAddress || rxSize <= 12 || txSize <= 12
        || rxSize > EC_MAXMBX || txSize > EC_MAXMBX
        || uint32_t(rxAddress) + rxSize > 0x10000u || uint32_t(txAddress) + txSize > 0x10000u
        || (rxAddress < uint32_t(txAddress) + txSize && txAddress < uint32_t(rxAddress) + rxSize)) {
        return fail(FirmwareFlashError::InvalidBootMailbox, "EEPROM 中的 BOOT 邮箱配置无效或读取失败");
    }
    auto& target = ctx_.slavelist[slave];
    target.SM[0].StartAddr = htoes(rxAddress);
    target.SM[0].SMlength = htoes(rxSize);
    target.SM[1].StartAddr = htoes(txAddress);
    target.SM[1].SMlength = htoes(txSize);
    target.mbx_wo = rxAddress; target.mbx_l = rxSize;
    target.mbx_ro = txAddress; target.mbx_rl = txSize;
    for (int sm = 0; sm < 2; ++sm) {
        const int wkc = ecx_FPWR(&ctx_.port, target.configadr, sm ? ECT_REG_SM1 : ECT_REG_SM0,
                                 sizeof(ec_smt), &target.SM[sm], EC_TIMEOUTRET);
        if (wkc != 1)
            return fail(FirmwareFlashError::BootMailboxWriteFailed,
                        "写入 BOOT 邮箱 SM" + std::to_string(sm) + " 失败", wkc);
    }
    return true;
}

bool FirmwareTool::enterBootMode(uint16_t slave)
{
    ctx_.slavelist[slave].state = EC_STATE_INIT;
    const int initWkc = ecx_writestate(&ctx_, slave);
    if (initWkc != 1 || !confirmState(slave, EC_STATE_INIT, EC_TIMEOUTSTATE * 4))
        return fail(FirmwareFlashError::InitFailed, "进入 INIT 失败", initWkc);
    if (!configBootMailbox(slave)) return false;

    ctx_.slavelist[slave].state = EC_STATE_BOOT;
    const int bootWkc = ecx_writestate(&ctx_, slave);
    if (bootWkc != 1 || !confirmState(slave, EC_STATE_BOOT, EC_TIMEOUTSTATE * 10))
        return fail(FirmwareFlashError::BootFailed, "进入 BOOT 失败", bootWkc);
    return true;
}

bool FirmwareTool::leaveBootMode(uint16_t slave)
{
    ctx_.slavelist[slave].state = EC_STATE_INIT;
    const int wkc = ecx_writestate(&ctx_, slave);
    if (wkc != 1)
        return fail(FirmwareFlashError::ActivationFailed, "固件下载完成，但发送退出 BOOT 请求失败", wkc);
    result_.activationRequested = true;
    // ECAT2CANFD resets both cores on BOOT -> INIT; INIT may be too brief to
    // observe. Do not turn that expected reset into an 8-second state timeout.
    return true;
}

bool FirmwareTool::flashFirmware(uint16_t slave, const std::filesystem::path& binFile)
{
    result_ = {};
    result_.slave = slave;
    if (slave == 0)
        return fail(FirmwareFlashError::InvalidSlave, "从站位置从 1 开始");
    std::vector<uint8_t> fw;
    if (!loadFile(binFile, fw) || fw.empty()) {
        return fail(FirmwareFlashError::InvalidFile, "固件文件不存在、为空、不可读或超过接口大小上限");
    }
    const bool ecat2CanfdFile = binFile.filename() == std::filesystem::path("ecat2canfd.bin");
    if (ecat2CanfdFile) {
        const auto packageError = ecat2CanfdEnvelopeError(fw);
        if (!packageError.empty()) return fail(FirmwareFlashError::InvalidPackage, packageError);
    }
    if (!inited_ && !init()) return false;
    if (slave > ctx_.slavecount)
        return fail(FirmwareFlashError::InvalidSlave, "从站位置超出扫描范围");
    const auto& target = ctx_.slavelist[slave];
    if (target.eep_man == 0 && target.eep_id == 0xAB125 && target.eep_rev == 3 && !ecat2CanfdFile)
        return fail(FirmwareFlashError::InvalidPackage,
                    "目标 ECAT2CANFD 需要配套的完整双核发布包，文件名必须为 ecat2canfd.bin");

    if (!enterBootMode(slave)) {
        return false;
    }

    const std::string filename = binFile.filename().string();
    const int ret = ecx_FOEwrite(&ctx_, slave, const_cast<char*>(filename.c_str()), 0,
                                 static_cast<int>(fw.size()), fw.data(), EC_TIMEOUTSTATE);
    if (ret <= 0) {
        const char* reason = ret == 0 ? "FoE 未完成邮箱交互（超时、持续 BUSY、链路异常或工作计数为 0）"
            : ret == -EC_ERR_TYPE_FOE_FILE_NOTFOUND ? "FoE 文件名被从站拒绝"
            : ret == -EC_ERR_TYPE_FOE_PACKETNUMBER ? "FoE 应答包号不匹配"
            : ret == -EC_ERR_TYPE_PACKET_ERROR ? "收到非预期的邮箱报文"
            : ret == -EC_ERR_TYPE_FOE_ERROR ? "从站返回 FoE ERROR"
            : "FoE 传输失败";
        // BOOT -> INIT is a whole-device reset on ECAT2CANFD, including on an
        // unsuccessful download. Leave the slave untouched on this path.
        return fail(FirmwareFlashError::TransferFailed, reason, ret);
    }
    result_.soemReturn = ret;
    result_.downloadComplete = true;
    if (!leaveBootMode(slave)) return false;
    result_.message = formatFirmwareFlashResult(result_);
    return true;
}

} // namespace soem_interface
