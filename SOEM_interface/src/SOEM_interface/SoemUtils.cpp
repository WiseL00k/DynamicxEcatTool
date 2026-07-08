#include "SOEM_interface/SoemUtils.h"

#include <cstdio>
#include <cstring>
#include <fstream>

extern "C" {
#include <soem/soem.h>
}

namespace soem_interface{

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
        return false;
    }

    if (ecx_config_init(&ctx_) <= 0) {
        ecx_close(&ctx_);
        return false;
    }

    ecx_statecheck(&ctx_, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE * 4);

    inited_ = true;
    return true;
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
    if (end < 0) {
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
    uint32_t data = ecx_readeeprom(&ctx_, slave, ECT_SII_BOOTRXMBX, EC_TIMEOUTEEP);

    ctx_.slavelist[slave].SM[0].StartAddr = LO_WORD(data);
    ctx_.slavelist[slave].SM[0].SMlength = HI_WORD(data);

    ctx_.slavelist[slave].mbx_wo = LO_WORD(data);
    ctx_.slavelist[slave].mbx_l = HI_WORD(data);

    data = ecx_readeeprom(&ctx_, slave, ECT_SII_BOOTTXMBX, EC_TIMEOUTEEP);

    ctx_.slavelist[slave].SM[1].StartAddr = LO_WORD(data);
    ctx_.slavelist[slave].SM[1].SMlength = HI_WORD(data);

    ctx_.slavelist[slave].mbx_ro = LO_WORD(data);
    ctx_.slavelist[slave].mbx_rl = HI_WORD(data);

    ecx_FPWR(&ctx_.port, ctx_.slavelist[slave].configadr, ECT_REG_SM0, sizeof(ec_smt), &ctx_.slavelist[slave].SM[0], EC_TIMEOUTRET);
    ecx_FPWR(&ctx_.port, ctx_.slavelist[slave].configadr, ECT_REG_SM1, sizeof(ec_smt), &ctx_.slavelist[slave].SM[1], EC_TIMEOUTRET);

    return true;
}

bool FirmwareTool::enterBootMode(uint16_t slave)
{
    ctx_.slavelist[slave].state = EC_STATE_INIT;
    ecx_writestate(&ctx_, slave);

    if (ecx_statecheck(&ctx_, slave, EC_STATE_INIT, EC_TIMEOUTSTATE * 4) != EC_STATE_INIT) {
        return false;
    }

    configBootMailbox(slave);

    ctx_.slavelist[slave].state = EC_STATE_BOOT;
    ecx_writestate(&ctx_, slave);

    return ecx_statecheck(&ctx_, slave, EC_STATE_BOOT, EC_TIMEOUTSTATE * 10) == EC_STATE_BOOT;
}

bool FirmwareTool::leaveBootMode(uint16_t slave)
{
    ctx_.slavelist[slave].state = EC_STATE_INIT;
    ecx_writestate(&ctx_, slave);

    return ecx_statecheck(&ctx_, slave, EC_STATE_INIT, EC_TIMEOUTSTATE * 4) == EC_STATE_INIT;
}

bool FirmwareTool::flashFirmware(uint16_t slave, const std::filesystem::path& binFile)
{
    if (!inited_ && !init()) {
        return false;
    }

    if (slave == 0 || slave > ctx_.slavecount) {
        return false;
    }

    std::vector<uint8_t> fw;
    if (!loadFile(binFile, fw) || fw.empty()) {
        return false;
    }

    if (!enterBootMode(slave)) {
        return false;
    }

    const std::string filename = binFile.filename().string();
    const int ret = ecx_FOEwrite(&ctx_, slave, const_cast<char*>(filename.c_str()), 0,
                                 static_cast<int>(fw.size()), fw.data(), EC_TIMEOUTSTATE);

    leaveBootMode(slave);
    return ret > 0;
}

} // namespace soem_interface
