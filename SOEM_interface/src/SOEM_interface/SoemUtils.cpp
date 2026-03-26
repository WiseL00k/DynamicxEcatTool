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

}
