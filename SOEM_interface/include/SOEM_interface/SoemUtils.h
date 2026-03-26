#ifndef SOEMUTILS_H
#define SOEMUTILS_H

#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
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
    /**
     * @brief SoemUtils
     *
     * 纯工具类：
     * - 不保存任何状态
     * - 不管理 EtherCAT 主站
     * - 只提供与 SOEM 相关的“系统级查询”功能
     */
    class SOEM_INTERFACE_EXPORT SoemUtils
    {
    public:
        struct AdapterInfo
        {
            std::string name;   ///< 用于 ec_init 的接口名
            std::string desc;   ///< 人类可读描述
        };

        /**
         * @brief 扫描当前系统中可用的 EtherCAT 网络适配器
         *
         * @return AdapterInfo 列表（快照）
         */
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
        InvaidEEpromHexFile
    };
}

class SOEM_INTERFACE_EXPORT EEpromTool
{
public:
    explicit EEpromTool(std::string ifname, int slave, int mode, std::string fname): ifname_(ifname), slave_(slave), mode_(mode), fname_(fname){}
    EEpromTool() = delete;

    error::SoemInterfaceErrorCode work(std::string ifname, int slave, int mode, std::string fname);

    void calc_crc(uint8 *crc, uint8 b)
    {
        int j;
        *crc ^= b;
        for (j = 0; j <= 7; j++)
        {
            if (*crc & 0x80)
                *crc = (*crc << 1) ^ 0x07;
            else
                *crc = (*crc << 1);
        }
    }

    uint16 SIIcrc(uint8 *buf)
    {
        int i;
        uint8 crc;

        crc = 0xff;
        for (i = 0; i <= 13; i++)
        {
            calc_crc(&crc, *(buf++));
        }
        return (uint16)crc;
    }

    int input_bin(const char *fname, int *length)
    {
        FILE *fp;

        int cc = 0, c;

        fp = fopen(fname, "rb");
        if (fp == NULL)
            return 0;
        while (((c = fgetc(fp)) != EOF) && (cc < MAXBUF))
            ebuf[cc++] = (uint8)c;
        *length = cc;
        fclose(fp);

        return 1;
    }

    int input_intelhex(const char *fname, int *start, int *length)
    {
        FILE *fp;

        int c, sc, retval = 1;
        int ll, ladr, lt, sn, i, lval;
        int hstart, hlength, sum;

        fp = fopen(fname, "r");
        if (fp == NULL)
            return 0;
        hstart = MAXBUF;
        hlength = 0;
        sum = 0;
        do
        {
            memset(sline, 0x00, MAXSLENGTH);
            sc = 0;
            while (((c = fgetc(fp)) != EOF) && (c != 0x0A) && (sc < (MAXSLENGTH - 1)))
                sline[sc++] = (uint8)c;
            if ((c != EOF) && ((sc < 11) || (sline[0] != ':')))
            {
                c = EOF;
                retval = 0;
                printf("Invalid Intel Hex format.\n");
            }
            if (c != EOF)
            {
                sn = sscanf(sline, ":%2x%4x%2x", &ll, &ladr, &lt);
                if ((sn == 3) && ((ladr + ll) <= MAXBUF) && (lt == 0))
                {
                    sum = ll + (ladr >> 8) + (ladr & 0xff) + lt;
                    if (ladr < hstart) hstart = ladr;
                    for (i = 0; i < ll; i++)
                    {
                        sn = sscanf(&sline[9 + (i << 1)], "%2x", &lval);
                        ebuf[ladr + i] = (uint8)lval;
                        sum += (uint8)lval;
                    }
                    if (((ladr + ll) - hstart) > hlength)
                        hlength = (ladr + ll) - hstart;
                    sum = (0x100 - sum) & 0xff;
                    sn = sscanf(&sline[9 + (i << 1)], "%2x", &lval);
                    if (!sn || ((sum - lval) != 0))
                    {
                        c = EOF;
                        retval = 0;
                        printf("Invalid checksum.\n");
                    }
                }
            }
        } while (c != EOF);
        if (retval)
        {
            *length = hlength;
            *start = hstart;
        }
        fclose(fp);

        return retval;
    }

    int output_bin(const char *fname, int length)
    {
        FILE *fp;

        int cc;

        fp = fopen(fname, "wb");
        if (fp == NULL)
            return 0;
        for (cc = 0; cc < length; cc++)
            fputc(ebuf[cc], fp);
        fclose(fp);

        return 1;
    }

    int output_intelhex(const char *fname, int length)
    {
        FILE *fp;

        int cc = 0, ll, sum, i;

        fp = fopen(fname, "w");
        if (fp == NULL)
            return 0;
        while (cc < length)
        {
            ll = length - cc;
            if (ll > IHEXLENGTH) ll = IHEXLENGTH;
            sum = ll + (cc >> 8) + (cc & 0xff);
            fprintf(fp, ":%2.2X%4.4X00", ll, cc);
            for (i = 0; i < ll; i++)
            {
                fprintf(fp, "%2.2X", ebuf[cc + i]);
                sum += ebuf[cc + i];
            }
            fprintf(fp, "%2.2X\n", (0x100 - sum) & 0xff);
            cc += ll;
        }
        fprintf(fp, ":00000001FF\n");
        fclose(fp);

        return 1;
    }

    int eeprom_read(int slave, int start, int length)
    {
        int i, ainc = 4;
        uint16 estat, aiadr;
        uint32 b4;
        uint64 b8;
        uint8 eepctl;

        if ((ctx.slavecount >= slave) && (slave > 0) && ((start + length) <= MAXBUF))
        {
            aiadr = 1 - slave;
            eepctl = 2;
            ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET); /* force Eeprom from PDI */
            eepctl = 0;
            ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET); /* set Eeprom to master */

            estat = 0x0000;
            aiadr = 1 - slave;
            ecx_APRD(&ctx.port, aiadr, ECT_REG_EEPSTAT, sizeof(estat), &estat, EC_TIMEOUTRET); /* read eeprom status */
            estat = etohs(estat);
            if (estat & EC_ESTAT_R64)
            {
                ainc = 8;
                for (i = start; i < (start + length); i += ainc)
                {
                    b8 = ecx_readeepromAP(&ctx, aiadr, i >> 1, EC_TIMEOUTEEP);
                    ebuf[i] = b8 & 0xFF;
                    ebuf[i + 1] = (b8 >> 8) & 0xFF;
                    ebuf[i + 2] = (b8 >> 16) & 0xFF;
                    ebuf[i + 3] = (b8 >> 24) & 0xFF;
                    ebuf[i + 4] = (b8 >> 32) & 0xFF;
                    ebuf[i + 5] = (b8 >> 40) & 0xFF;
                    ebuf[i + 6] = (b8 >> 48) & 0xFF;
                    ebuf[i + 7] = (b8 >> 56) & 0xFF;
                }
            }
            else
            {
                for (i = start; i < (start + length); i += ainc)
                {
                    b4 = ecx_readeepromAP(&ctx, aiadr, i >> 1, EC_TIMEOUTEEP) & 0xFFFFFFFF;
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

    int eeprom_write(int slave, int start, int length)
    {
        int i, dc = 0;
        uint16 aiadr, *wbuf;
        uint8 eepctl;

        if ((ctx.slavecount >= slave) && (slave > 0) && ((start + length) <= MAXBUF))
        {
            aiadr = 1 - slave;
            eepctl = 2;
            ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET); /* force Eeprom from PDI */
            eepctl = 0;
            ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET); /* set Eeprom to master */

            aiadr = 1 - slave;
            wbuf = (uint16 *)&ebuf[0];
            for (i = start; i < (start + length); i += 2)
            {
                ecx_writeeepromAP(&ctx, aiadr, i >> 1, *(wbuf + (i >> 1)), EC_TIMEOUTEEP);
                if (++dc >= 100)
                {
                    dc = 0;
                    printf(".");
                    fflush(stdout);
                }
            }

            return 1;
        }

        return 0;
    }

    int eeprom_writealias(int slave, int alias, uint16 crc)
    {
        uint16 aiadr;
        uint8 eepctl;
        int ret;

        if ((ctx.slavecount >= slave) && (slave > 0) && (alias <= 0xffff))
        {
            aiadr = 1 - slave;
            eepctl = 2;
            ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET); /* force Eeprom from PDI */
            eepctl = 0;
            ecx_APWR(&ctx.port, aiadr, ECT_REG_EEPCFG, sizeof(eepctl), &eepctl, EC_TIMEOUTRET); /* set Eeprom to master */

            ret = ecx_writeeepromAP(&ctx, aiadr, 0x04, alias, EC_TIMEOUTEEP);
            if (ret)
                ret = ecx_writeeepromAP(&ctx, aiadr, 0x07, crc, EC_TIMEOUTEEP);

            return ret;
        }

        return 0;
    }

private:
    uint8 ebuf[MAXBUF];
    uint8 ob;
    uint16 ow;
    int os;
    int slave_;
    int alias;
    ec_timet tstart, tend, tdif;
    int wkc;
    int mode_;
    char sline[MAXSLENGTH];
    ecx_contextt ctx;
    std::string ifname_, fname_;
};
}
#endif // SOEMUTILS_H
