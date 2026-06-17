#include "SOEM_interface/SoemUtils.h"

extern "C" {
#include <soem/soem.h>
}
namespace soem_interface{
    std::vector<SoemUtils::AdapterInfo> SoemUtils::scanAdapters()
    {
        std::vector<AdapterInfo> adapters;

        ec_adaptert* adapter = ec_find_adapters();
        ec_adaptert* current = adapter;

        while (current != nullptr)
        {
            adapters.push_back({
                current->name,
                current->desc
            });
            current = current->next;
        }

        ec_free_adapters(adapter);
        return adapters;
    }

    error::SoemInterfaceErrorCode EEpromTool::work(std::string ifname, int slave, int mode, std::string fname)
    {
        int w, rc = 0, estart, esize;
        uint16 *wbuf;

        /* initialise SOEM, bind socket to ifname */
        if (ecx_init(&ctx, ifname.c_str()))
        {
            printf("ecx_init on %s succeeded.\n", ifname.c_str());

            w = 0x0000;
            wkc = ecx_BRD(&ctx.port, 0x0000, ECT_REG_TYPE, sizeof(w), &w, EC_TIMEOUTSAFE); /* detect number of slaves */
            if (wkc > 0)
            {
                ctx.slavecount = wkc;

                printf("%d slaves found.\n", ctx.slavecount);
                if ((ctx.slavecount >= slave) && (slave > 0))
                {
                    if ((mode == MODE_INFO) || (mode == MODE_READBIN) || (mode == MODE_READINTEL))
                    {
                        tstart = osal_current_time();
                        eeprom_read(slave, 0x0000, MINBUF); // read first 128 bytes

                        wbuf = (uint16 *)&ebuf[0];
                        printf("Slave %d data\n", slave);
                        printf(" PDI Control      : %4.4X\n", *(wbuf + 0x00));
                        printf(" PDI Config       : %4.4X\n", *(wbuf + 0x01));
                        printf(" Config Alias     : %4.4X\n", *(wbuf + 0x04));
                        printf(" Checksum         : %4.4X\n", *(wbuf + 0x07));
                        printf("   calculated     : %4.4X\n", SIIcrc(&ebuf[0]));
                        printf(" Vendor ID        : %8.8X\n", *(uint32 *)(wbuf + 0x08));
                        printf(" Product Code     : %8.8X\n", *(uint32 *)(wbuf + 0x0A));
                        printf(" Revision Number  : %8.8X\n", *(uint32 *)(wbuf + 0x0C));
                        printf(" Serial Number    : %8.8X\n", *(uint32 *)(wbuf + 0x0E));
                        printf(" Mailbox Protocol : %4.4X\n", *(wbuf + 0x1C));
                        esize = (*(wbuf + 0x3E) + 1) * 128;
                        if (esize > MAXBUF) esize = MAXBUF;
                        printf(" Size             : %4.4X = %d bytes\n", *(wbuf + 0x3E), esize);
                        printf(" Version          : %4.4X\n", *(wbuf + 0x3F));
                    }
                    if ((mode == MODE_READBIN) || (mode == MODE_READINTEL))
                    {
                        if (esize > MINBUF)
                            eeprom_read(slave, MINBUF, esize - MINBUF); // read reminder

                        tend = osal_current_time();
                        osal_time_diff(&tstart, &tend, &tdif);
                        if (mode == MODE_READINTEL) output_intelhex(fname.c_str(), esize);
                        if (mode == MODE_READBIN) output_bin(fname.c_str(), esize);

                        printf("\nTotal EEPROM read time :%dms\n", (int)(tdif.tv_sec * 1000 + tdif.tv_nsec / 1000000));
                    }
                    if ((mode == MODE_WRITEBIN) || (mode == MODE_WRITEINTEL))
                    {
                        estart = 0;
                        if (mode == MODE_WRITEINTEL) rc = input_intelhex(fname.c_str(), &estart, &esize);
                        if (mode == MODE_WRITEBIN) rc = input_bin(fname.c_str(), &esize);

                        if (rc > 0)
                        {
                            wbuf = (uint16 *)&ebuf[0];
                            printf("Slave %d\n", slave);
                            printf(" Vendor ID        : %8.8X\n", *(uint32 *)(wbuf + 0x08));
                            printf(" Product Code     : %8.8X\n", *(uint32 *)(wbuf + 0x0A));
                            printf(" Revision Number  : %8.8X\n", *(uint32 *)(wbuf + 0x0C));
                            printf(" Serial Number    : %8.8X\n", *(uint32 *)(wbuf + 0x0E));

                            printf("Busy");
                            fflush(stdout);
                            tstart = osal_current_time();
                            eeprom_write(slave, estart, esize);
                            tend = osal_current_time();
                            osal_time_diff(&tstart, &tend, &tdif);

                            printf("\nTotal EEPROM write time :%dms\n", (int)(tdif.tv_sec * 1000 + tdif.tv_nsec / 1000000));
                        }
                        else
                        {
                            return error::InvaidEEpromHexFile;
                            printf("Error reading file, abort.\n");
                        }
                    }
                    if (mode == MODE_WRITEALIAS)
                    {
                        if (eeprom_read(slave, 0x0000, CRCBUF)) // read first 14 bytes
                        {
                            wbuf = (uint16 *)&ebuf[0];
                            *(wbuf + 0x04) = alias;
                            if (eeprom_writealias(slave, alias, SIIcrc(&ebuf[0])))
                            {
                                printf("Alias %4.4X written successfully to slave %d\n", alias, slave);
                            }
                            else
                            {
                                printf("Alias not written\n");
                            }
                        }
                        else
                        {
                            printf("Could not read slave EEPROM");
                        }
                    }
                }
                else
                {
                    printf("Slave number outside range.\n");
                }
            }
            else
            {
                printf("No slaves found!\n");
            }
            printf("End, close socket\n");
            /* stop SOEM, close socket */
            ecx_close(&ctx);
            return error::NoError;
        }
        else
        {
            printf("No socket connection on %s\nExcecute as root\n", ifname.c_str());
            return error::EcatInitFailed;
        }
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

    //====================================================
    // INIT / CLOSE
    //====================================================

    bool FirmwareTool::init()
    {
        if (inited_)
            return true;

        if (!ecx_init(&ctx_, ifname_.c_str()))
            return false;

        if (ecx_config_init(&ctx_) <= 0)
            return false;

        ecx_statecheck(&ctx_, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE * 4);

        inited_ = true;
        return true;
    }

    void FirmwareTool::close()
    {
        if (inited_)
        {
            ecx_close(&ctx_);
            inited_ = false;
        }
    }

    //====================================================
    // FILE LOAD
    //====================================================

    bool FirmwareTool::loadFile(
        const std::filesystem::path& file,
        std::vector<uint8_t>& buffer)
    {
        std::ifstream ifs(file, std::ios::binary);
        if (!ifs)
            return false;

        ifs.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(ifs.tellg());
        ifs.seekg(0);

        buffer.resize(size);

        ifs.read(reinterpret_cast<char*>(buffer.data()), size);

        return true;
    }

    //====================================================
    // BOOT MAILBOX CONFIG
    //====================================================

    bool FirmwareTool::configBootMailbox(uint16_t slave)
    {
        uint32_t data;

        data = ecx_readeeprom(
            &ctx_,
            slave,
            ECT_SII_BOOTRXMBX,
            EC_TIMEOUTEEP);

        ctx_.slavelist[slave].SM[0].StartAddr = LO_WORD(data);
        ctx_.slavelist[slave].SM[0].SMlength   = HI_WORD(data);

        ctx_.slavelist[slave].mbx_wo = LO_WORD(data);
        ctx_.slavelist[slave].mbx_l  = HI_WORD(data);

        data = ecx_readeeprom(
            &ctx_,
            slave,
            ECT_SII_BOOTTXMBX,
            EC_TIMEOUTEEP);

        ctx_.slavelist[slave].SM[1].StartAddr = LO_WORD(data);
        ctx_.slavelist[slave].SM[1].SMlength   = HI_WORD(data);

        ctx_.slavelist[slave].mbx_ro = LO_WORD(data);
        ctx_.slavelist[slave].mbx_rl = HI_WORD(data);

        ecx_FPWR(
            &ctx_.port,
            ctx_.slavelist[slave].configadr,
            ECT_REG_SM0,
            sizeof(ec_smt),
            &ctx_.slavelist[slave].SM[0],
            EC_TIMEOUTRET);

        ecx_FPWR(
            &ctx_.port,
            ctx_.slavelist[slave].configadr,
            ECT_REG_SM1,
            sizeof(ec_smt),
            &ctx_.slavelist[slave].SM[1],
            EC_TIMEOUTRET);

        return true;
    }

    //====================================================
    // ENTER BOOT
    //====================================================

    bool FirmwareTool::enterBootMode(uint16_t slave)
    {
        ctx_.slavelist[slave].state = EC_STATE_INIT;
        ecx_writestate(&ctx_, slave);

        if (ecx_statecheck(
                &ctx_,
                slave,
                EC_STATE_INIT,
                EC_TIMEOUTSTATE * 4) != EC_STATE_INIT)
            return false;

        configBootMailbox(slave);

        ctx_.slavelist[slave].state = EC_STATE_BOOT;
        ecx_writestate(&ctx_, slave);

        return ecx_statecheck(
                   &ctx_,
                   slave,
                   EC_STATE_BOOT,
                   EC_TIMEOUTSTATE * 10)
               == EC_STATE_BOOT;
    }

    //====================================================
    // LEAVE BOOT
    //====================================================

    bool FirmwareTool::leaveBootMode(uint16_t slave)
    {
        ctx_.slavelist[slave].state = EC_STATE_INIT;
        ecx_writestate(&ctx_, slave);

        return ecx_statecheck(
                   &ctx_,
                   slave,
                   EC_STATE_INIT,
                   EC_TIMEOUTSTATE * 4)
               == EC_STATE_INIT;
    }

    //====================================================
    // FLASH FIRMWARE
    //====================================================

    bool FirmwareTool::flashFirmware(
        uint16_t slave,
        const std::filesystem::path& binFile)
    {

        if (!inited_ && !init())
            return false;

        std::vector<uint8_t> fw;

        if (!loadFile(binFile, fw))
            return false;

        if (!enterBootMode(slave))
            return false;

        int ret = ecx_FOEwrite(
            &ctx_,
            slave,
            const_cast<char*>(binFile.filename().string().c_str()),
            0,
            fw.size(),
            fw.data(),
            EC_TIMEOUTSTATE);

        leaveBootMode(slave);
        return ret > 0;
    }
}
